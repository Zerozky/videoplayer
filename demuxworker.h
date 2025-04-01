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
#include <QAudioSink>
extern "C"
{
#include "libavutil/frame.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
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
    void sigUpdateTime(double currentTime, double totalTime);
    void sigCodecInfo(const QString &codecInfo);

public slots:
    void slotDoWork();
    void setFilePath(const QString &path);
    void seekTo(double newTime);       // 直接跳转到指定时间（秒）
    void seekRelative(double offset);    // 相对跳转（例如快退/快进）
    void setVolume(double volume);       // 设置音量，0～1

private:
    qint64 pauseAccumulated = 0; // 累计暂停的时间（毫秒）
    qint64 baseTime = 0;         // 播放基准时间，用于同步

    double pendingSeekTime = -1;         // 如果 >= 0，则表示有绝对跳转请求
    double pendingSeekOffset = 0;        // 累计的相对跳转请求（秒）
    QMutex seekMutex;

    // 用于 seek 后 flush 缓冲区
    AVFormatContext *m_avFormatContext = nullptr;
    AVCodecContext *m_videoCodecContext = nullptr;
    AVCodecContext *m_audioCodecContext = nullptr;

    // 便于实时调整音量）
    QAudioSink *m_audioSink = nullptr;

    double currentPlaybackTime = 0; // 保存当前播放时间（秒）
};

#endif // DEMUXWORKER_H
