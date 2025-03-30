#ifndef MEDIAWINDOW_H
#define MEDIAWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QDebug>
#include <QPushButton>
#include <QSplitter>
#include <QFrame>
#include <QResizeEvent>
#include <QFileDialog>
#include <QSlider>
#include "demuxworker.h"
#ifdef Q_OS_WIN
#include "cudavideowindow.h"
#endif
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
// #include <SDL2/SDL.h>
// #include <SDL2/SDL_video.h>
// #include <SDL2/SDL_render.h>
// #include <SDL2/SDL_rect.h>
}

class DemuxWorker;

class MediaWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MediaWindow(QWidget *parent = nullptr);
    ~MediaWindow() ;

signals:
    void sigStartPlay();
    void sigSetFilePath(const QString &filePath);

public slots:
    void startPlay();
    void stopPlay();
    void openFile();
    void togglePlayPause();
#ifdef Q_OS_WIN
    void updateCudaVideo(AVFrame *pFrame);
#else
    void updateVideo(AVFrame *pFrame);
#endif
    int initVideoDisplay(int mWidth,int mHeight);

private:
    // UI 部分
    QSplitter *mainSplitter;      // Main horizontal splitter
    QSplitter *leftSplitter;      // Left vertical splitter

    QFrame *videoFrame;           // Left top frame for video
    QFrame *controlFrame;         // Left bottom frame for controls
    QFrame *rightFrame;           // Right frame (播放列表)

#ifdef Q_OS_WIN
    CudaVideoWindow *cudaVideoWindow; // 用于 CUDA 渲染
#else
    QLabel *videoLabel; // 其他平台使用 QLabel
#endif

    QSlider *progressSlider;
    QSlider *volumeSlider;

    QPushButton *stopButton;
    QPushButton *openButton;
    QPushButton *playButton;      // 播放/暂停按钮
    QPushButton *rewindButton;    // 快退
    QPushButton *forwardButton;   // 快进

    QLabel *timeInfoLabel;        // 视频当前时间 / 总时长
    QLabel *codecInfoLabel;       // 音视频编码器信息

    QPushButton *toggleControlPanelButton; // 打开/隐藏控制面板
    QPushButton *togglePlaylistButton;     // 打开/关闭播放列表

    void setupUI();
    void setupControlPanel();
    QFrame* createFrame(QSizePolicy::Policy hPolicy, QSizePolicy::Policy vPolicy);
    QSlider* createSlider(int min, int max, int value);
    QPushButton* createButton(const QString& text);
    void setupConnections();    // 线程信号连接


    // 播放线程和解码器对象
    QThread *demuxThread = nullptr;
    DemuxWorker *demuxWorker = nullptr;
    QString selectedFilePath;

    // QImage 渲染方案时保存视频原始宽高
    int videoWidth = 0;
    int videoHeight = 0;

#ifdef Q_OS_WIN
    // 在 MediaWindow 类中增加成员变量
    SwsContext *m_sws_ctx = nullptr;
    uint8_t *m_conversionBuffer = nullptr;
    int m_conversionBufferSize = 0;
    AVFrame *m_frameRGBA = nullptr;
#endif
};

#endif // MEDIAWINDOW_H
