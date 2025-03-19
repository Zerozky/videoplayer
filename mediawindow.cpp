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
    // Create the main layout with splitters
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    leftSplitter = new QSplitter(Qt::Vertical);

    // Create the three frames
    videoFrame = new QFrame();
    videoFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    videoFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    controlFrame = new QFrame();
    controlFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    controlFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    rightFrame = new QFrame();
    rightFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    rightFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // Add the video label to the video frame
    videoLabel = new QLabel();
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLabel->setStyleSheet("background-color: black;");

    QVBoxLayout *videoLayout = new QVBoxLayout(videoFrame);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->addWidget(videoLabel);

    // Add control buttons to the control frame
    QVBoxLayout *controlLayout = new QVBoxLayout(controlFrame);
    controlLayout->setContentsMargins(5, 5, 5, 5);
    controlLayout->setSpacing(5);

    QHBoxLayout *row1Layout = new QHBoxLayout();
    progressSlider = new QSlider(Qt::Horizontal, controlFrame);
    progressSlider->setRange(0, 1000);
    progressSlider->setValue(0);
    row1Layout->addWidget(new QLabel("Progress:", controlFrame));
    row1Layout->addWidget(progressSlider, 1);
    volumeSlider = new QSlider(Qt::Horizontal, controlFrame);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(50);
    row1Layout->addWidget(new QLabel("Volume:", controlFrame));
    row1Layout->addWidget(volumeSlider, 0);
    controlLayout->addLayout(row1Layout);

    QHBoxLayout *row2Layout = new QHBoxLayout();
    playButton = new QPushButton("Play", controlFrame);
    row2Layout->addWidget(playButton);
    row2Layout->addWidget(stopButton);  // stopButton 已在前面创建
    rewindButton = new QPushButton("<<", controlFrame);
    row2Layout->addWidget(rewindButton);
    forwardButton = new QPushButton(">>", controlFrame);
    row2Layout->addWidget(forwardButton);
    row2Layout->addWidget(openButton);
    row2Layout->addStretch();
    timeInfoLabel = new QLabel("00:00 / 00:00", controlFrame);
    row2Layout->addWidget(timeInfoLabel);
    codecInfoLabel = new QLabel("HVC1 AAC", controlFrame);
    row2Layout->addWidget(codecInfoLabel);
    toggleControlPanelButton = new QPushButton("Hide Control Panel", controlFrame);
    row2Layout->addWidget(toggleControlPanelButton);
    togglePlaylistButton = new QPushButton("Close Playlist", controlFrame);
    row2Layout->addWidget(togglePlaylistButton);
    controlLayout->addLayout(row2Layout);

    // openButton = new QPushButton("Open File");
    // startButton = new QPushButton("Start");
    // pauseButton = new QPushButton("Pause");
    // stopButton = new QPushButton("Stop");

    // startButton->setEnabled(false);

    // QHBoxLayout *buttonLayout = new QHBoxLayout(controlFrame);
    // buttonLayout->setContentsMargins(0, 0, 0, 0);
    // buttonLayout->setSpacing(5);
    // buttonLayout->addWidget(openButton);
    // buttonLayout->addWidget(startButton);
    // buttonLayout->addWidget(pauseButton);
    // buttonLayout->addWidget(stopButton);

    // Set up the splitters
    leftSplitter->addWidget(videoFrame);
    leftSplitter->addWidget(controlFrame);

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(rightFrame);

    // Set initial splitter proportions
    leftSplitter->setStretchFactor(0, 10); // Video takes 4 parts
    leftSplitter->setStretchFactor(1, 1); // Controls take 1 part

    mainSplitter->setStretchFactor(0, 8); // Left part takes 3 parts
    mainSplitter->setStretchFactor(1, 2); // Right part takes 1 part

    // Set up the window
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainSplitter);

    this->setMinimumSize(800, 600);

    // Set up threads and connections
    demuxThread = new QThread;
    demuxWorker = new DemuxWorker;
    demuxWorker->moveToThread(demuxThread);

    connect(demuxThread, &QThread::finished, demuxWorker, &QObject::deleteLater);
    connect(demuxWorker, SIGNAL(sigGetVideoInfo(int,int)), this, SLOT(initSdl(int,int)));
    connect(demuxWorker, SIGNAL(sigGetFrame(AVFrame*)), this, SLOT(updateVideo(AVFrame*)));
    connect(this, SIGNAL(sigStartPlay()), demuxWorker, SLOT(slotDoWork()));
    connect(this, SIGNAL(sigSetFilePath(QString)), demuxWorker, SLOT(setFilePath(QString)));

    // connect(openButton, SIGNAL(clicked(bool)), this, SLOT(openFile()));
    // connect(startButton, SIGNAL(clicked(bool)), this, SLOT(startPlay()));
    // connect(pauseButton, SIGNAL(clicked(bool)), this, SLOT(pausePlay()));
    // connect(stopButton, &QPushButton::clicked, this, &MediaWindow::stopPlay);
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
    // Stop playback if already playing
    if (demuxThread->isRunning())
    {
        demuxWorker->isPlay = false;
        demuxThread->quit();
        demuxThread->wait();
        freeSdl();
        sdlInitialized = false;
    }

    // Open file dialog
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Video File"),
        QString(),
        tr("Video Files (*.mp4 *.mkv *.avi *.mov);;All Files (*.*)")
        );

    if (!filePath.isEmpty())
    {
        selectedFilePath = filePath;
        emit sigSetFilePath(selectedFilePath);
        startButton->setEnabled(true);

        // Update window title to show selected file
        setWindowTitle("Media Player - " + QFileInfo(selectedFilePath).fileName());
    }
}

void MediaWindow::startPlay()
{
    if (!demuxThread->isRunning() && !selectedFilePath.isEmpty())
    {
        demuxThread->start();
        emit sigStartPlay();
        startButton->setEnabled(false);
    }
}

void MediaWindow::pausePlay()
{
    if(demuxWorker->isPause)
    {
        demuxWorker->isPause = false;
        pauseButton->setText("pause");
    }
    else
    {
        demuxWorker->isPause = true;
        pauseButton ->setText("continue");
    }
}

void MediaWindow::stopPlay()
{
    QMessageBox::StandardButton button = QMessageBox::information(this,"提示","退出",QMessageBox::Ok,QMessageBox::Cancel);
    if(button==QMessageBox::Ok)
    {
        demuxWorker->isPlay = false;
        QEventLoop loop;
        QTimer::singleShot(1.5 * 1000, &loop, SLOT(quit()));
        QTimer::singleShot(2 * 1000, this, SLOT(close()));
        loop.exec();
    }
    qDebug()<< "stop play";
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
        if (sdlWindow) SDL_DestroyWindow(sdlWindow);
        sdlRenderer = nullptr;
        sdlTexture = nullptr;
        sdlWindow = nullptr;
        SDL_Quit();
        sdlInitialized = false;
    }
    return 0;
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
