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

    void ProcessingDatas(char *data, int count);
    bool ProcessingFrame(char *data, int count, QVector<float> &dd);
private:
    uint32_t image_count_mutation_count_ = 0;
};
#endif // JUSTCANFD_H
