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
#include <QFile>
#include <QTextStream>
#include <QTime>
extern "C"
{
#include "libavutil/frame.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
};

class DemuxWorker : public QObject
{
    Q_OBJECT
public:
    explicit DemuxWorker(QObject *parent = nullptr);
    void setStreamIndices(int videoIdx, int audioIdx, int subtitleIdx);
    double getcurrentPlaybackTime();
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
    void sigSubtitle(const QString &subtitleText, int durationMs);
    void sigSubtitleImage(const QImage &subtitleImage, int durationMs);

public slots:
    void slotDoWork();
    void setFilePath(const QString &path);
    void seekTo(double newTime);       // 直接跳转到指定时间（秒）
    void seekRelative(double offset);    // 相对跳转（例如快退/快进）
    void setVolume(double volume);       // 设置音量，0～1
    void setExternalSubtitle(const QString &path);
    void setPlaybackSpeed(double speed);

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
    SwrContext *swrCtx = nullptr;

    QAudioSink    *m_audioSink     = nullptr;      // 便于随时重建
    QIODevice     *m_audioIO       = nullptr;      // 写入音频的设备
    QAudioDevice   m_audioDevice;                  // 音频输出设备信息
    QAudioFormat   m_audioFormat;                  // 原始音频格式（用于重建）
    int            originalSampleRate = 0;         // 解码时的采样率

    double currentPlaybackTime = 0; // 保存当前播放时间（秒）
    double playbackSpeed = 1.0;

    int selectedVideoStream = -1;
    int selectedAudioStream = -1;
    int selectedSubtitleStream = -1;

    QString externalSubtitlePath;

    struct SubtitleEvent { qint64 startMs, endMs; QString text; };
    std::vector<SubtitleEvent> externalEvents;
    size_t externalEventIndex = 0;

    double lastSeekTimeSec = -1.0;
    double videoFrameRate      = 30.0;    // 视频流帧率（初始化时从流里读入）
};

#endif // DEMUXWORKER_H
