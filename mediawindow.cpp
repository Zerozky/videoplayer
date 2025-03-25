#include "mediawindow.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QEventLoop>
#include <QTimer>
#include <QSizePolicy>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

MediaWindow::MediaWindow(QWidget *parent) :
    QWidget(parent),
    sdlRenderer(nullptr),
    sdlTexture(nullptr),
    sdlWindow(nullptr),
    videoWidth(0),
    videoHeight(0),
    sdlInitialized(false)
{
    setupUI();

    demuxThread = new QThread;
    demuxWorker = new DemuxWorker;
    demuxWorker->moveToThread(demuxThread);

    setupConnections();

    // 按钮连接
    connect(stopButton, &QPushButton::clicked, this, &MediaWindow::stopPlay);
    connect(openButton, &QPushButton::clicked, this, &MediaWindow::openFile);
    connect(playButton, &QPushButton::clicked, this, &MediaWindow::togglePlayPause);
}

MediaWindow::~MediaWindow()
{
    freeSdl();
    if (demuxThread->isRunning())
    {
        demuxWorker->isPlay = false;
        demuxThread->quit();
        demuxThread->wait();
    }
}

void MediaWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void MediaWindow::openFile()
{
    // 如果之前已有 demuxThread 或 demuxWorker（不论是否正在运行），先清理干净
    if (demuxWorker) {
        // 如果线程正处于暂停状态，先唤醒它
        demuxWorker->pauseMutex.lock();
        demuxWorker->isPause = false;
        demuxWorker->pauseCondition.wakeAll();
        demuxWorker->pauseMutex.unlock();
        qDebug() << "demuxWorker 如果线程正处于暂停状态，先唤醒它";
    }
    if (demuxThread) {
        if(demuxWorker)
            demuxWorker->isPlay = false;  // 通知停止
        demuxThread->quit();
        demuxThread->wait();
        demuxThread->deleteLater();
        demuxThread = nullptr;
        qDebug() << "demuxThread clear";
    }
    if (demuxWorker) {
        demuxWorker->deleteLater();
        demuxWorker = nullptr;
        qDebug() << "demuxWorker clear";
    }
    // 释放 SDL 资源
    freeSdl();
    sdlInitialized = false;
    qDebug() << "SDL free";

    QTimer::singleShot(100, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
            nullptr,
            tr("Open Video File"),
            QString(),
            tr("Video Files (*.mp4 *.mkv *.avi *.mov);;All Files (*.*)"),
            nullptr,
            QFileDialog::DontUseNativeDialog
            );


        qDebug() << "mediawindow.cpp filename " << filePath;
        if (!filePath.isEmpty())
        {
            selectedFilePath = filePath;
            // 重新创建线程和 worker
            demuxThread = new QThread;
            demuxWorker = new DemuxWorker;
            demuxWorker->moveToThread(demuxThread);
            setupConnections();  // 重新建立连接

            qDebug() << "重新建立连接";

            emit sigSetFilePath(selectedFilePath);
            // 自动启动播放
            startPlay();

            // 启用并设置 playButton 文本为 "Pause"
            playButton->setEnabled(true);
            playButton->setText("Pause");

            // 更新窗口标题
            setWindowTitle("Media Player - " + QFileInfo(selectedFilePath).fileName());
        }
    });
}

void MediaWindow::togglePlayPause()
{
    if (demuxWorker)
    {
        // 切换暂停状态
        demuxWorker->pauseMutex.lock();
        demuxWorker->isPause = !demuxWorker->isPause;
        qDebug() << "togglePlayPause: isPause =" << demuxWorker->isPause;
        // 如果从暂停恢复，唤醒解码线程并重置计时器基准
        if(!demuxWorker->isPause) {
            demuxWorker->pauseCondition.wakeAll();
        }
        demuxWorker->pauseMutex.unlock();

        // 更新按钮文本
        if(demuxWorker->isPause)
            playButton->setText("Play");
        else
            playButton->setText("Pause");
    }
}

void MediaWindow::startPlay()
{
    if (!demuxThread->isRunning() && !selectedFilePath.isEmpty())
    {
        demuxThread->start();
        emit sigStartPlay();
        playButton->setText("pause");
        playButton->setEnabled(true);
    }
}

void MediaWindow::stopPlay()
{
    if (demuxWorker) {
        // 停止播放视频
        demuxWorker->isPlay = false;
        demuxThread->quit();
        demuxThread->wait();
    }
    freeSdl();
    sdlInitialized = false;

    // 将视频区域恢复为黑色（重置背景）
    videoLabel->setStyleSheet("background-color: black;");

    // 恢复按钮状态
    playButton->setEnabled(true);
    playButton->setText("Play");

    // 删除线程和 worker，并置空
    if(demuxThread) {
        demuxThread->deleteLater();
        demuxThread = nullptr;
    }
    if(demuxWorker) {
        demuxWorker->deleteLater();
        demuxWorker = nullptr;
    }
}

int MediaWindow::initSdl(int mWidth,int mHeight)
{
    videoWidth = mWidth;
    videoHeight = mHeight;

    // 设置主窗口初始大小
    this->resize(1280, 720);

    // 初始化 SDL
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        qDebug() << "SDL_Init failed";
        return -1;
    }

    // 创建窗体
    sdlWindow = SDL_CreateWindowFrom((void *)videoLabel->winId());
    if (sdlWindow == nullptr)
    {
        qDebug() << "SDL_CreateWindowFrom failed";
        return -1;
    }

    // 从窗体创建渲染器
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // 创建渲染器纹理
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, mWidth, mHeight);

    SDL_RenderSetLogicalSize(sdlRenderer, mWidth, mHeight);

    sdlInitialized = true;

    return 0;
}

int MediaWindow::freeSdl()
{
    if (sdlInitialized)
    {
        if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
        if (sdlTexture) SDL_DestroyTexture(sdlTexture);
#ifndef Q_OS_MAC
        if (sdlWindow) SDL_DestroyWindow(sdlWindow);
#endif
        sdlRenderer = nullptr;
        sdlTexture = nullptr;
        sdlWindow = nullptr;
        SDL_Quit();
        sdlInitialized = false;
    }
    return 0;
}

void MediaWindow::setupUI()
{
    // 创建主布局 Splitter
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    leftSplitter = new QSplitter(Qt::Vertical);

    // 创建三大区域 Frame
    videoFrame = createFrame(QSizePolicy::Expanding, QSizePolicy::Expanding);
    controlFrame = createFrame(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightFrame = createFrame(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // 视频显示区域
    videoLabel = new QLabel();
    videoLabel->setAttribute(Qt::WA_NativeWindow);
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLabel->setStyleSheet("background-color: black;");

    QVBoxLayout *videoLayout = new QVBoxLayout(videoFrame);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->addWidget(videoLabel);

    //控制面板
    setupControlPanel();

    // 组装 Splitter
    leftSplitter->addWidget(videoFrame);
    leftSplitter->addWidget(controlFrame);

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(rightFrame);

    // 设置 Splitter 拉伸因子
    leftSplitter->setStretchFactor(0, 10);
    leftSplitter->setStretchFactor(1, 1);

    mainSplitter->setStretchFactor(0, 8);
    mainSplitter->setStretchFactor(1, 2);

    // 主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainSplitter);

    this->setMinimumSize(800, 600);
}

void MediaWindow::setupConnections()
{
    connect(demuxThread, &QThread::finished, demuxWorker, &QObject::deleteLater);
    connect(demuxWorker, &DemuxWorker::sigGetVideoInfo, this, &MediaWindow::initSdl);
    connect(demuxWorker, &DemuxWorker::sigGetFrame, this, &MediaWindow::updateVideo);
    connect(this, &MediaWindow::sigStartPlay, demuxWorker, &DemuxWorker::slotDoWork);
    connect(this, &MediaWindow::sigSetFilePath, demuxWorker, &DemuxWorker::setFilePath);

}

void MediaWindow::setupControlPanel()
{
    QVBoxLayout *controlLayout = new QVBoxLayout(controlFrame);
    controlLayout->setContentsMargins(5, 5, 5, 5);
    controlLayout->setSpacing(5);

    // 第一排：进度条 + 音量
    QHBoxLayout *row1Layout = new QHBoxLayout();
    progressSlider = createSlider(0, 1000, 0);
    volumeSlider = createSlider(0, 100, 50);

    row1Layout->addWidget(new QLabel("Progress:", controlFrame));
    row1Layout->addWidget(progressSlider, 1);
    row1Layout->addWidget(new QLabel("Volume:", controlFrame));
    row1Layout->addWidget(volumeSlider, 0);

    controlLayout->addLayout(row1Layout);

    // 第二排：按钮
    QHBoxLayout *row2Layout = new QHBoxLayout();
    openButton = createButton("Open File");
    playButton = createButton("Play");
    stopButton = createButton("Stop");
    rewindButton = createButton("<<");
    forwardButton = createButton(">>");

    playButton->setEnabled(false);

    row2Layout->addWidget(playButton);
    row2Layout->addWidget(stopButton);
    row2Layout->addWidget(rewindButton);
    row2Layout->addWidget(forwardButton);
    row2Layout->addWidget(openButton);
    row2Layout->addStretch();

    // 信息标签
    timeInfoLabel = new QLabel("00:00 / 00:00", controlFrame);
    codecInfoLabel = new QLabel("HVC1 AAC", controlFrame);

    row2Layout->addWidget(timeInfoLabel);
    row2Layout->addWidget(codecInfoLabel);

    // 控制面板开关按钮
    toggleControlPanelButton = new QPushButton("Hide Control Panel", controlFrame);
    togglePlaylistButton = new QPushButton("Close Playlist", controlFrame);

    row2Layout->addWidget(toggleControlPanelButton);
    row2Layout->addWidget(togglePlaylistButton);

    controlLayout->addLayout(row2Layout);
}

QFrame *MediaWindow::createFrame(QSizePolicy::Policy hPolicy, QSizePolicy::Policy vPolicy)
{
    QFrame* frame = new QFrame();
    frame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    frame->setSizePolicy(hPolicy, vPolicy);
    return frame;
}

QSlider *MediaWindow::createSlider(int min, int max, int value)
{
    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(value);
    return slider;
}

QPushButton *MediaWindow::createButton(const QString &text)
{
    return new QPushButton(text);
}

void MediaWindow::updateVideo(AVFrame *pFrame)
{
    if (sdlInitialized && sdlTexture && sdlRenderer)
    {
        // Update texture with new frame data
        SDL_UpdateYUVTexture(sdlTexture, NULL,
                             pFrame->data[0], pFrame->linesize[0],
                             pFrame->data[1], pFrame->linesize[1],
                             pFrame->data[2], pFrame->linesize[2]);

        // 清除背景
        SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sdlRenderer);

        // 让 SDL 按“逻辑分辨率”自动缩放到整个窗口
        SDL_RenderCopy(sdlRenderer, sdlTexture, nullptr, nullptr);
        SDL_RenderPresent(sdlRenderer);
    }

    // 释放帧
    av_frame_free(&pFrame);
}
