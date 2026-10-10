#ifndef JUSTCANFD_H
#define JUSTCANFD_H

#include "dataengineinterface.h"

#include <QByteArray>
#include <QVector>
#include <cstdint>

class JustCanFd : public QObject, public DataEngineInterface
{
    Q_OBJECT
    Q_INTERFACES(DataEngineInterface)
    Q_PLUGIN_METADATA(IID "VOFA+.Plugin.JustCanFd")

public:
    explicit JustCanFd();
    ~JustCanFd();

    void ProcessingDatas(char *data, int count) override;

private:
    struct FastConfig {
        uint8_t node = 0;
        uint8_t id = 0;
        QVector<float> scales;
    };

    void ProcessMessage(const char *data, int len, int start, int end, uint16_t can_id);
    void ProcessResponse(const char *data, int len, int start, int end, uint8_t node);
    void ProcessFast(const char *data, int len, int start, int end, uint8_t node);
    void ProcessNormal(const char *data, int len, int start, int end);
    void InvalidFrame(int start, int end);
    static bool PlotScale(uint16_t var_id, float &scale);
    static bool PayloadValid(const char *data, int wire_len, int required_len);
    static int CanFdLength(int required_len);

    // Preserve incomplete USB frames between ProcessingDatas() calls.
    QByteArray pending_;
    // Keep recent configurations so packets still using the previous Config_ID
    // remain decodable during a running PLOT_CONFIG switch.
    QVector<FastConfig> fast_configs_;
};

#endif // JUSTCANFD_H
