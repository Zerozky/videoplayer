#ifndef DEMUXWORKER_H
#define DEMUXWORKER_H

#include <QThread>
#include <QImage>
#include <QLabel>
#include <QObject>
#include <QThread>
#include <QString>
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

signals:
    void sigGetFrame(AVFrame *pFrame);
    void sigGetVideoInfo(int mWidth,int mHeight);

public slots:
    void slotDoWork();
    void setFilePath(const QString &path);
};

#endif // DEMUXWORKER_H
