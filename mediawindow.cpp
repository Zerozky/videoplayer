#include "mediawindow.h"

#include "demuxworker.h"
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
#include <QWindow>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

MediaWindow::MediaWindow(QWidget *parent) :
    QWidget(parent)
{
    setupUI();
    setupConnections();

    // 按钮连接
    connect(stopButton, &QPushButton::clicked, this, &MediaWindow::stopPlay);
    connect(openButton, &QPushButton::clicked, this, &MediaWindow::openFile);
    connect(playButton, &QPushButton::clicked, this, &MediaWindow::togglePlayPause);

    // 连接进度条和音量滑块事件
    connect(progressSlider, &QSlider::sliderReleased, this, &MediaWindow::onProgressSliderReleased);
    connect(volumeSlider, &QSlider::valueChanged, this, &MediaWindow::onVolumeSliderChanged);
    connect(rewindButton, &QPushButton::clicked, this, &MediaWindow::onRewindButtonClicked);
    connect(forwardButton, &QPushButton::clicked, this, &MediaWindow::onForwardButtonClicked);
    connect(progressSlider, &QSlider::sliderPressed, this, [this]() {
        sliderBeingDragged = true;
    });

    // 右侧视频列表双击切换
    connect(videoListWidget, &QListWidget::itemDoubleClicked, this, &MediaWindow::onVideoListDoubleClicked);
    // DeepSeek 按钮点击
    connect(deepSeekButton, &QPushButton::clicked, this, &MediaWindow::onDeepSeekButtonClicked);

}

MediaWindow::~MediaWindow()
{
    // 停止播放，清理线程
    if (demuxThread && demuxThread->isRunning())
    {
        demuxWorker->isPlay = false;
        demuxThread->requestInterruption();
        demuxThread->quit();
        demuxThread->wait();
    }
    if(demuxThread) {
        delete demuxThread;
        demuxThread = nullptr;
    }
    if(demuxWorker) {
        delete demuxWorker;
        demuxWorker = nullptr;
    }
}

void MediaWindow::openFile()
{
    // 如果线程处于暂停状态，先断开并唤醒
    if (demuxWorker) {
        disconnect(demuxWorker, nullptr, this, nullptr);
        QMutexLocker locker(&demuxWorker->pauseMutex);
        demuxWorker->isPause = false;
        demuxWorker->pauseCondition.wakeAll();
    }
    // 如果已有线程，先停止播放并清理
    if (demuxThread) {
        if (demuxWorker)
            demuxWorker->isPlay = false;
        demuxThread->requestInterruption();
        demuxThread->quit();
        demuxThread->wait();
        demuxThread->deleteLater();
        demuxThread = nullptr;
        qDebug() << "demuxThread cleared";
    }
    if (demuxWorker) {
        demuxWorker->deleteLater();
        demuxWorker = nullptr;
        qDebug() << "demuxWorker cleared";
    }

#ifdef Q_OS_WIN
    // 释放转换资源
    if (m_sws_ctx) {
        sws_freeContext(m_sws_ctx);
        m_sws_ctx = nullptr;
    }
    if (m_conversionBuffer) {
        cudaFreeHost(m_conversionBuffer);
        m_conversionBuffer = nullptr;
        m_conversionBufferSize = 0;
    }
    if (m_frameRGBA) {
        av_frame_free(&m_frameRGBA);
        m_frameRGBA = nullptr;
    }
#endif

    // 处理完清理后，确保事件处理完毕
    QCoreApplication::processEvents();

    // 使用延时调用，确保旧资源已清理完毕
    QTimer::singleShot(100, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Open Video File"),
            QString(),
            tr("Video Files (*.mp4 *.mkv *.avi *.mov);;All Files (*.*)"),
            nullptr,
            QFileDialog::DontUseNativeDialog
            );

        qDebug() << "Selected file:" << filePath;
        if (!filePath.isEmpty())
        {
            selectedFilePath = filePath;
            // 创建新的线程和 worker
            demuxThread = new QThread;
            demuxWorker = new DemuxWorker;
            demuxWorker->moveToThread(demuxThread);
            setupConnections();  // 仅建立信号连接
            qDebug() << "重新建立连接";
            
            emit sigSetFilePath(selectedFilePath);
            startPlay();

            playButton->setEnabled(true);
            playButton->setText("Pause");

            setWindowTitle("Media Player - " + QFileInfo(selectedFilePath).fileName());

            // 更新右侧视频列表
            updateVideoList();
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

void MediaWindow::updateTimeLabel(double currentTime, double totalTime)
{
    totalDuration = totalTime;  // 保存总时长
    // 格式化时间（以秒为单位，转换为 mm:ss 格式）
    QTime current(0, 0);
    current = current.addSecs(static_cast<int>(currentTime));
    QTime total(0, 0);
    total = total.addSecs(static_cast<int>(totalTime));
    timeInfoLabel->setText(current.toString("mm:ss") + " / " + total.toString("mm:ss"));

    // 如果没有拖动 slider，则自动更新进度条
    if (!sliderBeingDragged && totalTime > 0) {
        int sliderValue = static_cast<int>((currentTime / totalTime) * progressSlider->maximum());
        progressSlider->setValue(sliderValue);
    }
}

void MediaWindow::updateCodecInfo(const QString &codecInfo)
{
    codecInfoLabel->setText(codecInfo);
}

void MediaWindow::onProgressSliderReleased()
{
    if(totalDuration > 0) {
        double newPos = (static_cast<double>(progressSlider->value()) / progressSlider->maximum()) * totalDuration;
        emit sigSeekTo(newPos);
    }
    sliderBeingDragged = false;
}

void MediaWindow::onVolumeSliderChanged(int value)
{
    // 将 0~100 的值转换为 0~1 的音量
    double volume = value / 100.0;
    emit sigSetVolume(volume);
}

void MediaWindow::onRewindButtonClicked()
{
    // 快退5秒
    emit sigSeekRelative(-5.0);
}

void MediaWindow::onForwardButtonClicked()
{
    // 快进5秒
    emit sigSeekRelative(5.0);
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
    // 断开与 demuxWorker 的所有信号连接，避免 pending 调用执行
    if(demuxWorker)
        disconnect(demuxWorker, nullptr, this, nullptr);

    if (demuxWorker) {
        // 通知 worker 停止播放
        demuxWorker->isPlay = false;
        // 如果处于暂停状态，先唤醒，防止线程阻塞
        demuxWorker->pauseCondition.wakeAll();
        // 请求线程中断并结束事件循环
        if (demuxThread) {
            demuxThread->requestInterruption();
            demuxThread->quit();
            demuxThread->wait();
        }
    }
    // 恢复视频区域显示
#ifdef Q_OS_WIN
    // Windows 下使用 CudaVideoWidget 清屏
    if (cudaVideoWindow)
        cudaVideoWindow->clearScreen();
#else
    // 其他平台使用原来的 QLabel 清空背景
    if (videoLabel)
        videoLabel->setStyleSheet("background-color: black;");
#endif
    // 恢复按钮状态
    playButton->setEnabled(true);
    playButton->setText("Play");

    // 安全删除线程和 worker，使用 deleteLater() 避免同步删除引发 pending 调用问题
    if (demuxThread) {
        demuxThread->deleteLater();
        demuxThread = nullptr;
        qDebug() << "demuxThread cleared";
    }
    if (demuxWorker) {
        demuxWorker->deleteLater();
        demuxWorker = nullptr;
        qDebug() << "demuxWorker cleared";
    }

#ifdef Q_OS_WIN
    // 释放转换相关资源
    if (m_sws_ctx) {
        sws_freeContext(m_sws_ctx);
        m_sws_ctx = nullptr;
    }
    if (m_conversionBuffer) {
        cudaFreeHost(m_conversionBuffer);
        m_conversionBuffer = nullptr;
        m_conversionBufferSize = 0;
    }
    if (m_frameRGBA) {
        av_frame_free(&m_frameRGBA);
        m_frameRGBA = nullptr;
    }
#endif
}

int MediaWindow::initVideoDisplay(int mWidth,int mHeight)
{
    videoWidth = mWidth;
    videoHeight = mHeight;
    qDebug() << "initVideoDisplay: videoWidth =" << videoWidth << ", videoHeight =" << videoHeight;

    return 0;
}

void MediaWindow::onVideoListDoubleClicked(QListWidgetItem *item)
{
    if(item) {
        QString newFilePath = item->data(Qt::UserRole).toString();
        if(!newFilePath.isEmpty() && newFilePath != selectedFilePath) {
            // 停止当前播放
            stopPlay();
            // 更新文件路径
            selectedFilePath = newFilePath;
            // 更新窗口标题
            setWindowTitle("Media Player - " + QFileInfo(selectedFilePath).fileName());
            // 启动播放（相当于 openFile 后直接播放）
            demuxThread = new QThread;
            demuxWorker = new DemuxWorker;
            demuxWorker->moveToThread(demuxThread);
            setupConnections();
            emit sigSetFilePath(selectedFilePath);
            startPlay();
        }
    }
}

void MediaWindow::onDeepSeekButtonClicked()
{
    QString query = deepSeekLineEdit->text().trimmed();
    if(query.isEmpty()) {
        deepSeekResultText->setText("请输入查询内容。");
        return;
    }

    // 构造 DeepSeek API 请求（根据 API 文档，调整 URL 和参数）
    QUrl url("https://api.deepseek.com/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 添加授权头部，替换为你的实际 API Key
    request.setRawHeader("Authorization", "Bearer sk-350bb32b02434688add2b505e5eb5eb0");

    // 构造 JSON 请求体
    // 系统消息
    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = "You are a helpful assistant.";
    // 用户消息
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = query;

    QJsonArray messages;
    messages.append(systemMessage);
    messages.append(userMessage);

    QJsonObject json;
    json["model"] = "deepseek-chat";
    json["messages"] = messages;
    json["stream"] = false; // 不使用流式返回

    QJsonDocument doc(json);
    QByteArray postData = doc.toJson();

    // 创建网络管理器并发起 POST 请求
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->post(request, postData);

    // 当请求完成后解析响应
    connect(reply, &QNetworkReply::finished, [=](){
        if(reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument responseDoc = QJsonDocument::fromJson(response);
            QJsonObject responseObj = responseDoc.object();
            QString answer;
            if(responseObj.contains("choices")){
                QJsonArray choices = responseObj.value("choices").toArray();
                if(!choices.isEmpty()){
                    QJsonObject firstChoice = choices.first().toObject();
                    if(firstChoice.contains("message")){
                        QJsonObject messageObj = firstChoice.value("message").toObject();
                        answer = messageObj.value("content").toString();
                    }
                }
            }
            if(answer.isEmpty()){
                answer = "未返回答案，请检查查询参数或 API 状态。";
            }
            deepSeekResultText->setText(answer);
        } else {
            deepSeekResultText->setText("请求失败: " + reply->errorString());
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

void MediaWindow::updateVideoList()
{
    videoListWidget->clear();
    // 获取当前视频文件所在目录
    QFileInfo fileInfo(selectedFilePath);
    QDir dir = fileInfo.absoluteDir();
    // 筛选视频文件
    QStringList filters;
    filters << "*.mp4" << "*.mkv" << "*.avi" << "*.mov";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : fileList) {
        // 仅显示与当前视频不同的文件
        if(fi.absoluteFilePath() != selectedFilePath) {
            QListWidgetItem *item = new QListWidgetItem(fi.fileName(), videoListWidget);
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            videoListWidget->addItem(item);
        }
    }
}

void MediaWindow::setupUI()
{
    // 创建主布局 Splitter
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    leftSplitter = new QSplitter(Qt::Vertical);

    // 创建左侧两个区域 Frame
    videoFrame = createFrame(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoFrame->setAttribute(Qt::WA_NativeWindow);
    controlFrame = createFrame(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 创建右侧区域使用 QTabWidget
    rightTabWidget = new QTabWidget(this);
    // 第一个页面：视频列表
    videoListWidget = new QListWidget(rightTabWidget);
    videoListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    // 初始为空，后续可在切换视频时更新列表
    rightTabWidget->addTab(videoListWidget, tr("Other Videos"));

    // 第二个页面：DeepSeek 对话
    deepSeekPage = new QWidget(rightTabWidget);
    QVBoxLayout *deepSeekLayout = new QVBoxLayout(deepSeekPage);
    deepSeekLineEdit = new QLineEdit(deepSeekPage);
    deepSeekLineEdit->setPlaceholderText(tr("Enter DeepSeek query..."));
    deepSeekButton = new QPushButton(tr("Search"), deepSeekPage);
    deepSeekResultText = new QTextEdit(deepSeekPage);
    deepSeekResultText->setReadOnly(true);
    deepSeekLayout->addWidget(deepSeekLineEdit);
    deepSeekLayout->addWidget(deepSeekButton);
    deepSeekLayout->addWidget(deepSeekResultText);
    deepSeekPage->setLayout(deepSeekLayout);
    rightTabWidget->addTab(deepSeekPage, tr("DeepSeek"));

    // 视频显示区域
#ifdef Q_OS_WIN
    // 创建 CudaVideoWindow，并通过 createWindowContainer 嵌入到 QWidget 布局中
    cudaVideoWindow = new CudaVideoWindow();
    QWidget *videoContainer = QWidget::createWindowContainer(cudaVideoWindow, this);
    videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *videoLayout = new QVBoxLayout;
    videoLayout->setContentsMargins(0,0,0,0);
    videoLayout->addWidget(videoContainer);
    // 使用一个容器 Frame 存放 cudaVideoWidget
    videoFrame = new QFrame();
    videoFrame->setLayout(videoLayout);
#else
    videoLabel = new QLabel();
    videoLabel->setAttribute(Qt::WA_NativeWindow);
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    videoLabel->setStyleSheet("background-color: black;");
    QVBoxLayout *videoLayout = new QVBoxLayout;
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->addWidget(videoLabel);
    videoFrame = new QFrame();
    videoFrame->setLayout(videoLayout);
#endif

    //控制面板
    setupControlPanel();

    // 组装 Splitter
    leftSplitter->addWidget(videoFrame);
    leftSplitter->addWidget(controlFrame);

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(rightTabWidget);

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
    connect(demuxWorker, &DemuxWorker::sigGetVideoInfo, this, &MediaWindow::initVideoDisplay);
#ifdef Q_OS_WIN
    connect(demuxWorker, &DemuxWorker::sigGetFrame, this, &MediaWindow::updateCudaVideo);
#else
    connect(demuxWorker, &DemuxWorker::sigGetFrame, this, &MediaWindow::updateVideo);
#endif
    connect(this, &MediaWindow::sigStartPlay, demuxWorker, &DemuxWorker::slotDoWork);
    connect(this, &MediaWindow::sigSetFilePath, demuxWorker, &DemuxWorker::setFilePath);

    // 新增：接收播放时间更新和编解码器信息（需要在 DemuxWorker 中实现相应信号）
    connect(demuxWorker, &DemuxWorker::sigUpdateTime, this, &MediaWindow::updateTimeLabel);
    connect(demuxWorker, &DemuxWorker::sigCodecInfo, this, &MediaWindow::updateCodecInfo);

    connect(this, &MediaWindow::sigSeekTo, demuxWorker, &DemuxWorker::seekTo, Qt::QueuedConnection);
    connect(this, &MediaWindow::sigSeekRelative, demuxWorker, &DemuxWorker::seekRelative, Qt::QueuedConnection);
    connect(this, &MediaWindow::sigSetVolume, demuxWorker, &DemuxWorker::setVolume, Qt::QueuedConnection);
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
    slider->setTracking(true);  // 确保拖动时能发送信号
    return slider;
}

QPushButton *MediaWindow::createButton(const QString &text)
{
    return new QPushButton(text);
}

#ifdef Q_OS_WIN
void MediaWindow::updateCudaVideo(AVFrame *pFrame)
{
    if (!pFrame)
        return;

    // 使用原始视频的宽高
    int dstW = videoWidth;
    int dstH = videoHeight;

    // 初始化或复用 sws 上下文
    if (!m_sws_ctx) {
        m_sws_ctx = sws_getCachedContext(nullptr,
                                         videoWidth, videoHeight, (AVPixelFormat)pFrame->format,
                                         dstW, dstH, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws_ctx) {
            qDebug() << "无法创建 sws 上下文";
            av_frame_free(&pFrame);
            return;
        }
    }

    // 计算转换缓冲区所需大小，并分配/复用页锁定内存
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, dstW, dstH, 1);
    if (numBytes != m_conversionBufferSize) {
        if (m_conversionBuffer) {
            cudaFreeHost(m_conversionBuffer);
            m_conversionBuffer = nullptr;
        }
        cudaError_t err = cudaHostAlloc((void**)&m_conversionBuffer, numBytes, cudaHostAllocDefault);
        if (err != cudaSuccess) {
            qDebug() << "cudaHostAlloc failed:" << cudaGetErrorString(err);
            av_frame_free(&pFrame);
            return;
        }
        m_conversionBufferSize = numBytes;
    }

    // 分配或复用 pFrameRGBA 用于存储转换结果
    if (!m_frameRGBA) {
        m_frameRGBA = av_frame_alloc();
        if (!m_frameRGBA) {
            qDebug() << "无法分配 m_frameRGBA";
            av_frame_free(&pFrame);
            return;
        }
    }
    av_image_fill_arrays(m_frameRGBA->data, m_frameRGBA->linesize,
                         m_conversionBuffer, AV_PIX_FMT_RGBA, dstW, dstH, 1);

    // 进行颜色空间转换
    sws_scale(m_sws_ctx,
              pFrame->data, pFrame->linesize, 0, videoHeight,
              m_frameRGBA->data, m_frameRGBA->linesize);

    // 更新 CUDA 纹理
    cudaVideoWindow->updateFrame(m_frameRGBA->data[0], dstW, dstH);

    // 释放输入帧，不释放转换缓冲区与 pFrameRGBA，以便下次复用
    av_frame_free(&pFrame);
}
#else
void MediaWindow::updateVideo(AVFrame *pFrame)
{
    if (!pFrame) {
        return;
    }

    // 获取 videoLabel 的当前尺寸
    QSize labelSize = videoLabel->size();

    // 根据原始视频尺寸（videoWidth, videoHeight）计算目标尺寸，保持原比例
    double videoAspect = static_cast<double>(videoWidth) / videoHeight;
    int targetW = labelSize.width();
    int targetH = labelSize.height();
    double labelAspect = static_cast<double>(targetW) / targetH;
    if (videoAspect > labelAspect) {
        // 视频更宽，则以 label 宽度为基准
        targetH = static_cast<int>(targetW / videoAspect);
    } else {
        // 视频更高，则以 label 高度为基准
        targetW = static_cast<int>(targetH * videoAspect);
    }

    // 使用 sws_getCachedContext 来复用转换上下文
    // 原始尺寸 videoWidth x videoHeight，目标尺寸为 targetW x targetH，转换格式固定为 RGB24
    static SwsContext *sws_ctx = nullptr;
    sws_ctx = sws_getCachedContext(sws_ctx,
                                   videoWidth, videoHeight, (AVPixelFormat)pFrame->format,
                                   targetW, targetH, AV_PIX_FMT_RGB24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        qDebug() << "无法创建 sws 上下文";
        av_frame_free(&pFrame);
        return;
    }

    // 为目标 RGB 帧分配内存（尺寸为目标显示尺寸）
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, targetW, targetH, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
    if (!buffer) {
        qDebug() << "无法分配转换缓冲区";
        av_frame_free(&pFrame);
        return;
    }
    // 创建目标 RGB 帧
    AVFrame *pFrameRGB = av_frame_alloc();
    if (!pFrameRGB) {
        qDebug() << "无法分配 pFrameRGB";
        av_free(buffer);
        av_frame_free(&pFrame);
        return;
    }
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, targetW, targetH, 1);

    // 进行颜色空间转换，同时直接缩放到目标尺寸
    sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, videoHeight,
              pFrameRGB->data, pFrameRGB->linesize);

    // 创建 QImage
    QImage image(pFrameRGB->data[0], targetW, targetH, pFrameRGB->linesize[0], QImage::Format_RGB888);
    QImage imageCopy = image.copy();

    // 释放转换过程分配的资源
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrame);

    // 在 GUI 线程中更新 videoLabel
    QMetaObject::invokeMethod(videoLabel, [=]() {
            videoLabel->setPixmap(QPixmap::fromImage(imageCopy));
        }, Qt::QueuedConnection);
}
#endif
