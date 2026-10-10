#include "../justcanfd.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QVector>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace {
void Check(bool passed)
{
    if (!passed)
        throw std::runtime_error("JustCanFd regression failed");
}

int Dlc(int required)
{
    if (required <= 8)
        return required;
    for (int length : {12, 16, 20, 24, 32, 48, 64}) {
        if (required <= length)
            return length;
    }
    throw std::runtime_error("test payload too large");
}

void U16(QByteArray &data, uint16_t value)
{
    data.append(char(value));
    data.append(char(value >> 8));
}

void F32(QByteArray &data, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    U16(data, uint16_t(bits));
    U16(data, uint16_t(bits >> 16));
}

QByteArray Wrap(int type, QByteArray payload, int node = 1)
{
    const int size = Dlc(payload.size());
    payload.append(size - payload.size(), '\0');
    QByteArray frame("AXDR", 4);
    U16(frame, uint16_t((type << 6) | node));
    frame.append(char(size));
    frame.append(payload);
    return frame;
}

QByteArray Config(int id, const QVector<uint16_t> &ids, int node = 1)
{
    QByteArray body;
    body.append(char(1)); // Txn
    body.append(char(0x04)); // Plot
    body.append(char(0x01)); // Config
    body.append(char(0)); // OK
    body.append(char(0)); // Fast group
    body.append(char(id));
    body.append(char(ids.size()));
    for (auto v : ids)
        U16(body, v);
    return Wrap(0x02, body, node);
}

QByteArray Fast(int id, int channels, int samples, int node = 1)
{
    QByteArray body;
    U16(body, 123);
    body.append(char(id));
    body.append(char(samples));
    for (int i = 0; i < channels * samples; ++i)
        U16(body, uint16_t(100 + i));
    return Wrap(0x18, body, node);
}

QByteArray Normal(int channels)
{
    QByteArray body;
    U16(body, 1);
    body.append(char(1));
    body.append(char(channels));
    for (int i = 0; i < channels; ++i)
        F32(body, float(i) + 0.25f);
    return Wrap(0x10, body);
}

void Feed(JustCanFd &engine, QByteArray packet)
{
    engine.ProcessingDatas(packet.data(), packet.size());
}

bool Close(float a, float b)
{
    return std::fabs(a - b) < 0.002f;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    JustCanFd engine;
    const QVector<uint16_t> ids = {
        0x0001, 0x0002, 0x0014, 0x0021,
        0x0022, 0x0023, 0x0B05, 0x0510
    };
    const QVector<float> scales = {
        0.001f, 0.001f, 0.0002f, 0.1f,
        0.0002f, 0.1f, 0.001f, 0.001f
    };

    // Every legal NORMAL channel count, including DLC padding at 6 channels.
    for (int channels = 1; channels <= 15; ++channels) {
        Feed(engine, Normal(channels));
        Check(engine.frame_list().size() == 1);
        const Frame &f = engine.frame_list().first();
        Check(f.is_valid_ && f.datas_.size() == channels);
        for (int ch = 0; ch < channels; ++ch)
            Check(Close(f.datas_[ch], float(ch) + 0.25f));
    }

    // No guessing channels from an unconfigured FAST frame.
    Feed(engine, Fast(200, 2, 2));
    Check(engine.frame_list().size() == 1 && !engine.frame_list().first().is_valid_);

    // Every FAST channel count and its padded PLOT_CONFIG response.
    for (int channels = 1; channels <= 8; ++channels) {
        QVector<uint16_t> selected;
        for (int ch = 0; ch < channels; ++ch)
            selected.append(ids[ch]);
        Feed(engine, Config(channels, selected));
        Feed(engine, Fast(channels, channels, 2));
        Check(engine.frame_list().size() == 2);
        for (int sample = 0; sample < 2; ++sample) {
            const Frame &f = engine.frame_list()[sample];
            Check(f.is_valid_ && f.datas_.size() == channels);
            for (int ch = 0; ch < channels; ++ch)
                Check(Close(f.datas_[ch], (100 + sample * channels + ch) * scales[ch]));
        }
    }

    // Hot configuration: old frames may still arrive after a new response.
    Feed(engine, Fast(2, 2, 2));
    Check(engine.frame_list().size() == 2 && Close(engine.frame_list()[0].datas_[0], 0.1f));
    Feed(engine, Fast(8, 8, 2));
    Check(engine.frame_list().size() == 2 && Close(engine.frame_list()[0].datas_[3], 10.3f));

    // Unknown capability IDs must remain raw, not receive arbitrary scaling.
    Feed(engine, Config(49, QVector<uint16_t>{0xeaaa}));
    Feed(engine, Fast(49, 1, 1));
    Check(engine.frame_list().size() == 1);
    Check(engine.frame_list()[0].is_valid_ && engine.frame_list()[0].datas_[0] == 100.0f);

    // Same Config_ID on another node must not reuse another node's channels.
    Feed(engine, Config(8, QVector<uint16_t>{0x0014}, 2));
    Feed(engine, Fast(8, 1, 1, 2));
    Check(engine.frame_list().size() == 1 && Close(engine.frame_list()[0].datas_[0], 0.02f));

    // Reject non-zero CAN FD padding.
    QByteArray malformed = Normal(6);
    malformed[malformed.size() - 1] = char(1);
    Feed(engine, malformed);
    Check(engine.frame_list().size() == 1 && !engine.frame_list()[0].is_valid_);

    // Reject invalid DLC; the following valid frame still resynchronizes.
    QByteArray bad("AXDR", 4);
    U16(bad, 0x401);
    bad.append(char(9));
    bad.append(QByteArray(9, '\0'));
    bad.append(Normal(2));
    Feed(engine, bad);
    Check(!engine.frame_list().isEmpty() && engine.frame_list().last().is_valid_);

    // Split USB frame is retained until the final bytes arrive.
    const QByteArray full = Normal(6);
    QByteArray part1 = full.left(11);
    QByteArray part2 = full.mid(11);
    Feed(engine, part1);
    Check(engine.frame_list().isEmpty());
    Feed(engine, part2);
    Check(engine.frame_list().size() == 1 && engine.frame_list()[0].is_valid_);

    // Several complete frames inside one CDC/UDP chunk.
    Feed(engine, Normal(2) + Normal(6));
    Check(engine.frame_list().size() == 2);
    Check(engine.frame_list()[0].is_valid_ && engine.frame_list()[1].is_valid_);
    return 0;
}
