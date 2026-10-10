#include "justcanfd.h"

#include <cstring>
#include <initializer_list>

namespace {
constexpr int USB_HEADER_SIZE = 7;
constexpr int AXDR_MAX_DATA_LEN = 64;
constexpr int MAX_FAST_CHANNELS = 8;
constexpr int MAX_NORMAL_CHANNELS = 15;
constexpr int MAX_RECENT_CONFIGS = 8;

constexpr uint8_t MSG_RESPONSE = 0x02;
constexpr uint8_t MSG_NORMAL_DATA = 0x10;
constexpr uint8_t MSG_FAST_DATA = 0x18;
constexpr uint8_t MSG_PLOT = 0x04;
constexpr uint8_t PLOT_CONFIG = 0x01;
constexpr uint8_t STATUS_OK = 0x00;
constexpr uint8_t PLOT_FAST = 0x00;

const char AXDR_MAGIC[] = "AXDR";

uint16_t ReadU16(const char *data)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(data[0])) |
           (static_cast<uint16_t>(static_cast<uint8_t>(data[1])) << 8);
}
}

JustCanFd::JustCanFd() = default;
JustCanFd::~JustCanFd() = default;

int JustCanFd::CanFdLength(int required_len)
{
    if (required_len < 0 || required_len > AXDR_MAX_DATA_LEN)
        return -1;
    if (required_len <= 8)
        return required_len;
    for (int len : {12, 16, 20, 24, 32, 48, 64}) {
        if (required_len <= len)
            return len;
    }
    return -1;
}

bool JustCanFd::PayloadValid(const char *data, int wire_len, int required_len)
{
    if (CanFdLength(required_len) != wire_len)
        return false;
    for (int i = required_len; i < wire_len; ++i) {
        if (data[i] != 0)
            return false;
    }
    return true;
}

void JustCanFd::InvalidFrame(int start, int end)
{
    Frame frame;
    frame.start_index_ = start;
    frame.end_index_ = end;
    frame.is_valid_ = false;
    frame_list_.append(frame);
}

void JustCanFd::ProcessingDatas(char *data, int count)
{
    frame_list_.clear();
    if (data == nullptr || count <= 0)
        return;

    const int carried = pending_.size();
    const QByteArray bytes = pending_ + QByteArray(data, count);
    pending_.clear();
    int pos = 0;

    while (pos < bytes.size()) {
        const int magic = bytes.indexOf(AXDR_MAGIC, pos);
        if (magic < 0) {
            // Preserve a possible partial magic at the end of the stream.
            pending_ = bytes.right(qMin(3, bytes.size() - pos));
            break;
        }
        if (magic + USB_HEADER_SIZE > bytes.size()) {
            pending_ = bytes.mid(magic);
            break;
        }

        const uint16_t can_id = ReadU16(bytes.constData() + magic + 4);
        const int len = static_cast<uint8_t>(bytes[magic + 6]);
        if (can_id > 0x07ff || CanFdLength(len) != len) {
            if (magic >= carried)
                InvalidFrame(magic - carried, magic - carried);
            pos = magic + 1;
            continue;
        }

        const int frame_end = magic + USB_HEADER_SIZE + len;
        if (frame_end > bytes.size()) {
            pending_ = bytes.mid(magic);
            break;
        }

        // Frames split across reads still produce plots, but do not point the
        // VOFA raw-byte view at data that belongs to a previous read.
        const int start = (magic >= carried) ? magic - carried : count;
        const int end = (magic >= carried) ? frame_end - carried - 1 : count - 1;
        ProcessMessage(bytes.constData() + magic + USB_HEADER_SIZE, len,
                       start, end, can_id);
        pos = frame_end;
    }
}

void JustCanFd::ProcessMessage(const char *data, int len, int start, int end, uint16_t can_id)
{
    const uint8_t msg_type = static_cast<uint8_t>((can_id >> 6) & 0x1f);
    const uint8_t node = static_cast<uint8_t>(can_id & 0x3f);
    switch (msg_type) {
    case MSG_RESPONSE:
        ProcessResponse(data, len, start, end, node);
        break;
    case MSG_FAST_DATA:
        ProcessFast(data, len, start, end, node);
        break;
    case MSG_NORMAL_DATA:
        ProcessNormal(data, len, start, end);
        break;
    default:
        InvalidFrame(start, end);
        break;
    }
}

void JustCanFd::ProcessResponse(const char *data, int len, int start, int end, uint8_t node)
{
    if (len >= 7 &&
        static_cast<uint8_t>(data[1]) == MSG_PLOT &&
        static_cast<uint8_t>(data[2]) == PLOT_CONFIG &&
        static_cast<uint8_t>(data[3]) == STATUS_OK &&
        static_cast<uint8_t>(data[4]) == PLOT_FAST) {

        const uint8_t config_id = static_cast<uint8_t>(data[5]);
        const uint8_t channels = static_cast<uint8_t>(data[6]);
        const int required = 7 + channels * 2;

        if (channels >= 1 && channels <= MAX_FAST_CHANNELS &&
            PayloadValid(data, len, required)) {
            FastConfig config;
            config.node = node;
            config.id = config_id;
            for (int ch = 0; ch < channels; ++ch) {
                const uint16_t id = ReadU16(data + 7 + ch * 2);
                float scale = 1.0f; // Unknown IDs remain unscaled raw counts.
                PlotScale(id, scale);
                config.scales.append(scale);
            }

            for (int i = fast_configs_.size() - 1; i >= 0; --i) {
                if (fast_configs_[i].node == node && fast_configs_[i].id == config_id)
                    fast_configs_.removeAt(i);
            }
            fast_configs_.prepend(config);
            if (fast_configs_.size() > MAX_RECENT_CONFIGS)
                fast_configs_.removeLast();
        }
    }
    // Response frames have no plot samples.
    InvalidFrame(start, end);
}

void JustCanFd::ProcessFast(const char *data, int len, int start, int end, uint8_t node)
{
    if (len < 4) {
        InvalidFrame(start, end);
        return;
    }

    const uint8_t config_id = static_cast<uint8_t>(data[2]);
    const int samples = static_cast<uint8_t>(data[3]);
    const FastConfig *config = nullptr;
    for (const FastConfig &candidate : fast_configs_) {
        if (candidate.node == node && candidate.id == config_id) {
            config = &candidate;
            break;
        }
    }

    // The DLC includes padding. Never guess channel count from its length.
    if (config == nullptr || samples == 0 ||
        !PayloadValid(data, len, 4 + samples * config->scales.size() * 2)) {
        InvalidFrame(start, end);
        return;
    }

    const int channels = config->scales.size();
    for (int sample = 0; sample < samples; ++sample) {
        Frame frame;
        // Only the first sample owns the raw packet in the VOFA hex view.
        frame.start_index_ = (sample == 0) ? start : end + 1;
        frame.end_index_ = end;
        frame.is_valid_ = true;
        for (int ch = 0; ch < channels; ++ch) {
            const int offset = 4 + (sample * channels + ch) * 2;
            const int16_t raw = static_cast<int16_t>(ReadU16(data + offset));
            frame.datas_.append(static_cast<float>(raw) * config->scales[ch]);
        }
        frame_list_.append(frame);
    }
}

void JustCanFd::ProcessNormal(const char *data, int len, int start, int end)
{
    if (len < 4) {
        InvalidFrame(start, end);
        return;
    }

    const int channels = static_cast<uint8_t>(data[3]);
    if (channels < 1 || channels > MAX_NORMAL_CHANNELS ||
        !PayloadValid(data, len, 4 + channels * 4)) {
        InvalidFrame(start, end);
        return;
    }

    Frame frame;
    frame.start_index_ = start;
    frame.end_index_ = end;
    frame.is_valid_ = true;
    for (int ch = 0; ch < channels; ++ch) {
        const char *value_data = data + 4 + ch * 4;
        uint32_t bits = static_cast<uint32_t>(static_cast<uint8_t>(value_data[0])) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(value_data[1])) << 8) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(value_data[2])) << 16) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(value_data[3])) << 24);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        frame.datas_.append(value);
    }
    frame_list_.append(frame);
}

bool JustCanFd::PlotScale(uint16_t var_id, float &scale)
{
    switch (var_id) {
    case 0x0001: // ADC Ia
    case 0x0002: // ADC Ib
    case 0x0003: // ADC Ic
    case 0x0010: // Id
    case 0x0011: // Iq
    case 0x0012: // Ud
    case 0x0013: // Uq
    case 0x0015: // Ualpha
    case 0x0016: // Ubeta
    case 0x0024: // Mechanical ESO Td
    case 0x0510: // Iq Ref
    case 0x0B05: // Signal injection output
    case 0x0B06: // Injected Id Ref
    case 0x0B07: // Id PI output
        scale = 0.001f;
        return true;
    case 0x0014: // FOC electrical angle
    case 0x0020: // Flux observer angle
    case 0x0022: // Mechanical ESO angle
    case 0x0025: // Mechanical ESO angle error
    case 0xF001: // Internal identification angle diagnostic
        scale = 0.0002f;
        return true;
    case 0x0021: // Flux observer electrical speed
    case 0x0023: // Mechanical ESO mechanical speed
        scale = 0.1f;
        return true;
    default:
        return false;
    }
}
