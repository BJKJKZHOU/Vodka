#include "justcanfd.h"

#include <cstring>


namespace
{
constexpr int USB_HEADER_SIZE = 7;
constexpr int AXDR_MAX_DATA_LEN = 64;

constexpr uint8_t MSG_NORMAL_DATA = 0x10;
constexpr uint8_t MSG_FAST_DATA = 0x18;

const char AXDR_MAGIC[4] = {'A', 'X', 'D', 'R'};
}


JustCanFd::JustCanFd()
{
}


JustCanFd::~JustCanFd()
{
}


void JustCanFd::ProcessingDatas(char *data, int count)
{
    frame_list_.clear();

    int pos = 0;

    while (pos + 4 <= count) {
        int magic = pos;

        while (magic + 4 <= count && std::memcmp(data + magic, AXDR_MAGIC, 4) != 0)
            magic++;

        if (magic > pos) {
            Frame frame;
            frame.start_index_ = pos;
            frame.end_index_ = magic - 1;
            frame.is_valid_ = false;
            frame_list_.append(frame);
        }

        if (magic + 4 > count)
            return;

        if (magic + USB_HEADER_SIZE > count)
            return;

        uint16_t can_id;
        std::memcpy(&can_id, data + magic + 4, sizeof(can_id));

        const uint8_t len = static_cast<uint8_t>(data[magic + 6]);

        if (can_id > 0x07FF || len > AXDR_MAX_DATA_LEN) {
            Frame frame;
            frame.start_index_ = magic;
            frame.end_index_ = magic;
            frame.is_valid_ = false;
            frame_list_.append(frame);
            pos = magic + 1;
            continue;
        }

        const int frame_size = USB_HEADER_SIZE + len;

        if (magic + frame_size > count)
            return;

        const int end = magic + frame_size - 1;
        ProcessMessage(data + magic + USB_HEADER_SIZE, len, magic, end, can_id);

        pos = end + 1;
    }
}


void JustCanFd::ProcessMessage(const char *data, int len, int start, int end, uint16_t can_id)
{
    const uint8_t msg_type = static_cast<uint8_t>((can_id >> 6) & 0x1F);

    if (msg_type == MSG_FAST_DATA) {
        ProcessFast(data, len, start, end);
        return;
    }

    if (msg_type == MSG_NORMAL_DATA) {
        ProcessNormal(data, len, start, end);
        return;
    }

    Frame frame;
    frame.start_index_ = start;
    frame.end_index_ = end;
    frame.is_valid_ = false;
    frame_list_.append(frame);
}


void JustCanFd::ProcessFast(const char *data, int len, int start, int end)
{
    if (len < 4)
        return;

    const uint8_t sample_count = static_cast<uint8_t>(data[3]);
    const int payload_len = len - 4;

    if (sample_count == 0 || payload_len <= 0 || payload_len % (sample_count * 2) != 0)
        return;

    const int channel_count = payload_len / (sample_count * 2);

    if (channel_count <= 0 || channel_count > 8)
        return;

    const char *sample_data = data + 4;

    for (int sample = 0; sample < sample_count; sample++) {
        Frame frame;
        frame.start_index_ = start;
        frame.end_index_ = end;
        frame.is_valid_ = true;

        for (int channel = 0; channel < channel_count; channel++) {
            int16_t raw;
            const int offset = (sample * channel_count + channel) * 2;
            std::memcpy(&raw, sample_data + offset, sizeof(raw));

            // V1 parser first exposes raw int16 counts as VOFA float channels.
            // Applying physical Plot_Scale requires the active PLOT_CONFIG mapping.
            frame.datas_.append(static_cast<float>(raw));
        }

        frame_list_.append(frame);
    }
}


void JustCanFd::ProcessNormal(const char *data, int len, int start, int end)
{
    if (len < 4)
        return;

    const uint8_t channel_count = static_cast<uint8_t>(data[3]);
    const int expected_len = 4 + channel_count * 4;

    if (channel_count == 0 || channel_count > 15 || expected_len != len)
        return;

    Frame frame;
    frame.start_index_ = start;
    frame.end_index_ = end;
    frame.is_valid_ = true;

    const char *channel_data = data + 4;

    for (int channel = 0; channel < channel_count; channel++) {
        float value;
        std::memcpy(&value, channel_data + channel * 4, sizeof(value));
        frame.datas_.append(value);
    }

    frame_list_.append(frame);
}
