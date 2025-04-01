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
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QDir>
#include <QNetworkAccessManager>
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
    void sigSeekTo(double newTime);         // 直接跳转
    void sigSetVolume(double volume);         // 设置音量0~1
    void sigSeekRelative(double offset);      // 相对跳转（快退/快进）

public slots:
    void startPlay();
    void stopPlay();
    void openFile();
    void togglePlayPause();
    void updateTimeLabel(double currentTime, double totalTime);
    void updateCodecInfo(const QString &codecInfo);
    void onProgressSliderReleased();
    void onVolumeSliderChanged(int value);
    void onRewindButtonClicked();
    void onForwardButtonClicked();
#ifdef Q_OS_WIN
    void updateCudaVideo(AVFrame *pFrame);
#else
    void updateVideo(AVFrame *pFrame);
#endif
    int initVideoDisplay(int mWidth,int mHeight);

    // 用于右侧页面的槽
    void onVideoListDoubleClicked(QListWidgetItem *item);
    void onDeepSeekButtonClicked();
    void updateVideoList(); // 更新右侧视频列表

private:
    // UI 部分
    QSplitter *mainSplitter;      // 主分割器
    QSplitter *leftSplitter;      // 左侧垂直分割器

    QFrame *videoFrame;           // 左上视频区域
    QFrame *controlFrame;         // 左下控制面板

    // 右侧区域，包含两个页面
    QTabWidget *rightTabWidget;
    // 第一个页面：视频列表
    QListWidget *videoListWidget;
    // 第二个页面：DeepSeek 对话
    QWidget *deepSeekPage;
    QLineEdit *deepSeekLineEdit;
    QPushButton *deepSeekButton;
    QTextEdit *deepSeekResultText;

#ifdef Q_OS_WIN
    CudaVideoWindow *cudaVideoWindow; // Windows平台用CUDA渲染
#else
    QLabel *videoLabel; // 其他平台使用QLabel
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

    double totalDuration = 0; // 保存视频总时长（单位：秒）
    bool sliderBeingDragged = false;  // 标记进度条是否正在被拖动

    QNetworkAccessManager *m_networkManager = nullptr;

#ifdef Q_OS_WIN
    SwsContext *m_sws_ctx = nullptr;
    uint8_t *m_conversionBuffer = nullptr;
    int m_conversionBufferSize = 0;
    AVFrame *m_frameRGBA = nullptr;
#endif
};

#endif // MEDIAWINDOW_H
