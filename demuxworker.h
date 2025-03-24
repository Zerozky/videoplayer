#ifndef DEMUXWORKER_H
#define DEMUXWORKER_H

#include <QThread>
#include <QImage>
#include <QLabel>
#include <QObject>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QWaitCondition>
extern "C"
{
#include "libavutil/frame.h"
};

class DemuxWorker : public QObject
{
    Q_OBJECT
public:
    explicit DemuxWorker(QObject *parent = nullptr);
    ~DemuxWorker();

public:
    bool isPlay;
    bool isPause;
    QString filePath;

    QMutex pauseMutex;
    QWaitCondition pauseCondition;

signals:
    void sigGetFrame(AVFrame *pFrame);
    void sigGetVideoInfo(int mWidth,int mHeight);

public slots:
    void slotDoWork();
    void setFilePath(const QString &path);

private:
    qint64 pauseAccumulated = 0; // 累计暂停的时间（毫秒）
    qint64 baseTime = 0;         // 播放基准时间，用于同步
};

#endif // DEMUXWORKER_H
