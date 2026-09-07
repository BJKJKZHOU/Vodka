#ifndef JUSTCANFD_H
#define JUSTCANFD_H

#include "dataengineinterface.h"


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
    void ProcessMessage(const char *data, int len, int start, int end, uint16_t can_id);
    void ProcessResponse(const char *data, int len, int start, int end);
    void ProcessFast(const char *data, int len, int start, int end);
    void ProcessNormal(const char *data, int len, int start, int end);
    bool PlotScale(uint16_t var_id, float &scale) const;

    uint8_t fast_config_id_ = 0;
    uint8_t fast_channel_count_ = 0;
    QVector<float> fast_scales_;
    bool fast_config_known_ = false;
    bool fast_config_valid_ = false;
};

#endif // JUSTCANFD_H
