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
#include "titlebar.h"
#ifdef Q_OS_WIN
#include "cudavideowindow.h"
#endif
#include "controlpaneldialog.h"
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
    ControlPanelDialog *controlDialog;

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void sigStartPlay();
    void sigSetFilePath(const QString &filePath);
    void sigSeekTo(double newTime);         // 直接跳转
    void sigSetVolume(double volume);         // 设置音量0~1
    void sigSeekRelative(double offset);      // 相对跳转（快退/快进）
    void sigLoadExternalSubtitle(const QString &path);

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

    void onSelectStreamButtonClicked();
    void slotDisplaySubtitle(const QString &subtitleText, int durationMs);
    void slotDisplaySubtitleImage(const QImage &subtitleImage, int durationMs);
    void onLoadSubtitleButtonClicked();
    void toggleFullScreen();

private:
    // UI 部分
    QSplitter *mainSplitter;      // 主分割器
    QSplitter *leftSplitter;      // 左侧垂直分割器

    QFrame *videoFrame;           // 左上视频区域
    QFrame *controlFrame;         // 左下控制面板

    // 标题栏
    TitleBar *titleBar;
    QWidget *overlayContainer;

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

    QSlider *progressSlider;    // 进度条
    QSlider *volumeSlider;      // 音量条

    QPushButton *playButton;    // 播放、暂停按钮
    QPushButton *stopButton;    // 停止按钮
    QPushButton *forwardButton; // 快进按钮
    QPushButton *rewindButton;  // 快退按钮
    QPushButton *openButton;    // 打开文件按钮
    QPushButton *toggleControlPanelButton;  // 打开、隐藏控制面板

    QLabel *timeInfoLabel;      // 视频当前时间 / 总时长
    QLabel *codecInfoLabel;     // 音视频编码器信息
    QLabel* subtitleLabel;      // 横排字幕
    QLabel* verticalSubtitleLabel;  // 竖排字幕

    void setupUI();
    void setupControlPanel();
    QSlider* createSlider(int min, int max, int value);

    QThread *demuxThread = nullptr;     // 播放线程
    DemuxWorker *demuxWorker = nullptr; // 解码器
    void setupConnections();    // 线程信号连接

    QString selectedFilePath;   // 视频文件路径
    int videoWidth = 0;     // 视频原始宽度
    int videoHeight = 0;    // 视频原始高度
    double totalDuration = 0; // 视频总时长（秒）
    bool sliderBeingDragged = false;  // 进度条是否被拖动

    QNetworkAccessManager *m_networkManager = nullptr;

#ifdef Q_OS_WIN
    SwsContext *m_sws_ctx = nullptr;
    uint8_t *m_conversionBuffer = nullptr;
    int m_conversionBufferSize = 0;
    AVFrame *m_frameRGBA = nullptr;
#endif

    void restartPlaybackAfterStreamSelection();
    QString getStreamDescription(AVStream *stream);

    int selectedVideoStream;    // 选择的视频流
    int selectedAudioStream;    // 选择的音频流
    int selectedSubtitleStream; // 选择的字幕流
    QTimer *subtitleClearTimer;     // 横排字幕计时器
    QTimer* verticalClearTimer;     // 竖排字幕计时器
    int pausedSubtitleRemaining = 0;
    int pausedVerticalRemaining = 0;
    QString externalSubtitlePath;    // 外挂字幕文件路径

    void loadFile(const QString &filePath);

};

#endif // MEDIAWINDOW_H
