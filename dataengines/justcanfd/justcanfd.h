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
    void ProcessFast(const char *data, int len, int start, int end);
    void ProcessNormal(const char *data, int len, int start, int end);
};

#endif // JUSTCANFD_H
