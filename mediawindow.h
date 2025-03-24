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
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_rect.h>
}

class MediaWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MediaWindow(QWidget *parent = nullptr);
    ~MediaWindow() ;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Main layout components
    QSplitter *mainSplitter;      // Main horizontal splitter
    QSplitter *leftSplitter;      // Left vertical splitter
    QFrame *videoFrame;           // Left top frame for video
    QFrame *controlFrame;         // Left bottom frame for controls
    QFrame *rightFrame;           // Right frame (empty for now)

    // Video components
    QLabel *videoLabel;           // Widget to attach SDL renderer

    // Control components
    QPushButton *pauseButton;
    QPushButton *stopButton;
    QPushButton *openButton;

    QSlider *progressSlider;
    QSlider *volumeSlider;
    QPushButton *playButton;      // 播放/暂停按钮
    QPushButton *rewindButton;    // 快退
    QPushButton *forwardButton;   // 快进
    QLabel *timeInfoLabel;        // 视频当前时间 / 总时长
    QLabel *codecInfoLabel;       // 音视频编码器信息
    QPushButton *toggleControlPanelButton; // 打开/隐藏控制面板
    QPushButton *togglePlaylistButton;     // 打开/关闭播放列表

    // Thread components
    QThread *demuxThread;
    DemuxWorker *demuxWorker;

    // SDL components
    SDL_Renderer *sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Window *sdlWindow;
    int videoWidth;
    int videoHeight;
    bool sdlInitialized;

    QString selectedFilePath;

    int freeSdl();

    void setupUI();
    void setupConnections();
    void setupControlPanel();

    // 辅助函数：创建 Frame
    QFrame* createFrame(QSizePolicy::Policy hPolicy, QSizePolicy::Policy vPolicy);
    // 辅助函数：创建 Slider
    QSlider* createSlider(int min, int max, int value);
    // 辅助函数：创建 Button
    QPushButton* createButton(const QString& text);


signals:
    void sigStartPlay();
    void sigSetFilePath(const QString &filePath);

public slots:
    void startPlay();
    void pausePlay();
    void stopPlay();
    void openFile();
    void updateVideo(AVFrame *pFrame);
    int initSdl(int mWidth,int mHeight);
};

#endif // MEDIAWINDOW_H
