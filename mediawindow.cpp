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
#include <QComboBox>
#include <QStackedLayout>
#include <QGraphicsDropShadowEffect>
#include <QRegularExpression>
#include "titlebar.h"
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

MediaWindow::MediaWindow(QWidget *parent) :
    QWidget(parent)
{
    setWindowFlags(
        Qt::Window
        | Qt::FramelessWindowHint
        | Qt::WindowSystemMenuHint
        | Qt::WindowMinimizeButtonHint
        | Qt::WindowMaximizeButtonHint
        );

    // 创建标题栏
    titleBar = new TitleBar(this);
    titleBar->setObjectName("titleBar");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    // 信号连接
    connect(titleBar, &TitleBar::sigMinimized, this, &MediaWindow::showMinimized);
    connect(titleBar, &TitleBar::sigtoggleMaximized, this, [this](){
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(titleBar, &TitleBar::sigclose, this, &MediaWindow::close);


    m_networkManager = new QNetworkAccessManager(this);
    // 控制面板窗口
    controlDialog = new ControlPanelDialog(this);
    controlDialog->hide();

    setupUI();

    // 有关DemuxWorker的链接
    setupConnections();

    // 控制台
    connect(progressSlider, &QSlider::sliderReleased, this, &MediaWindow::onProgressSliderReleased);
    connect(progressSlider, &QSlider::sliderPressed, this, [this]() {
        sliderBeingDragged = true;
    });
    connect(volumeSlider, &QSlider::valueChanged, this, &MediaWindow::onVolumeSliderChanged);
    connect(playButton, &QPushButton::clicked, this, &MediaWindow::togglePlayPause);
    connect(stopButton, &QPushButton::clicked, this, &MediaWindow::stopPlay);
    connect(rewindButton, &QPushButton::clicked, this, &MediaWindow::onRewindButtonClicked);
    connect(forwardButton, &QPushButton::clicked, this, &MediaWindow::onForwardButtonClicked);
    connect(openButton, &QPushButton::clicked, this, &MediaWindow::openFile);

    // 右侧视频列表双击切换
    connect(videoListWidget, &QListWidget::itemDoubleClicked, this, &MediaWindow::onVideoListDoubleClicked);
    // DeepSeek提问按钮
    connect(deepSeekButton, &QPushButton::clicked, this, &MediaWindow::onDeepSeekButtonClicked);

    // 横排字幕清除
    subtitleClearTimer = new QTimer(this);
    subtitleClearTimer->setSingleShot(true);
    connect(subtitleClearTimer, &QTimer::timeout, this, [this]() {
        subtitleLabel->clear();
    });
    // 竖排字幕清除
    verticalClearTimer = new QTimer(this);
    verticalClearTimer->setSingleShot(true);
    connect(verticalClearTimer, &QTimer::timeout, this, [this]() {
        verticalSubtitleLabel->clear();
    });

    // 点击按钮弹出/隐藏控制面板
    connect(toggleControlPanelButton, &QPushButton::clicked, this, [this](){
        if (controlDialog->isVisible()) {
            controlDialog->hide();
        } else {
            controlDialog->move(this->geometry().center() - controlDialog->rect().center());
            controlDialog->show();
        }
    });

    // 控制面板信号
    // “选择流”按钮
    connect(controlDialog, &ControlPanelDialog::selectStreamRequested,
            this, &MediaWindow::onSelectStreamButtonClicked);
    // “加载外挂字幕”按钮
    connect(controlDialog, &ControlPanelDialog::loadSubtitleRequested,
            this, &MediaWindow::onLoadSubtitleButtonClicked);
    // 快进快退按钮
    connect(controlDialog, &ControlPanelDialog::seekRelative,
            this, [this](double off){ emit sigSeekRelative(off); });
    // 字幕字体/字号
    connect(controlDialog, &ControlPanelDialog::subtitleFontChanged, this, [this](const QFont &f){
        subtitleLabel->setFont(f);
        verticalSubtitleLabel->setFont(f);
    });
    connect(controlDialog, &ControlPanelDialog::subtitleFontSizeChanged, this, [this](int sz){
        QFont f = subtitleLabel->font();
        f.setPointSize(sz);
        subtitleLabel->setFont(f);
        verticalSubtitleLabel->setFont(f);
    });
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
    // 1) 禁用 Open 按钮，防止重入
    openButton->setEnabled(false);

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

    stopPlay();
    // 弹系统对话框
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open Video File"), {},
        tr("Video Files (*.mp4 *.mkv *.avi *.mov);;All Files (*.*)")
        );
    openButton->setEnabled(true);
    if (path.isEmpty()) return;
    // 用统一接口载入
    loadFile(path);
    // 更新侧边列表
    updateVideoList();
}

void MediaWindow::togglePlayPause()
{
    if (!demuxWorker) return;

    // 切换暂停状态
    demuxWorker->pauseMutex.lock();
    demuxWorker->isPause = !demuxWorker->isPause;
    // 如果从暂停恢复，唤醒解码线程并重置计时器基准
    if(!demuxWorker->isPause) {
        demuxWorker->pauseCondition.wakeAll();
    }
    demuxWorker->pauseMutex.unlock();

    // —— 新增：暂停时 stop() 并记下剩余；恢复时 restart() ——
    if (demuxWorker->isPause) {
        pausedSubtitleRemaining = subtitleClearTimer->remainingTime();
        subtitleClearTimer->stop();
        pausedVerticalRemaining = verticalClearTimer->remainingTime();
        verticalClearTimer->stop();
    } else {
        if (pausedSubtitleRemaining > 0)
            subtitleClearTimer->start(pausedSubtitleRemaining);
        if (pausedVerticalRemaining > 0)
            verticalClearTimer->start(pausedVerticalRemaining);
    }

    // 更新按钮文本
    playButton->setIcon(QIcon(demuxWorker->isPause ? ":/icons/play" : ":/icons/pause"));
}

void MediaWindow::updateTimeLabel(double currentTime, double totalTime)
{
    totalDuration = totalTime;  // 保存总时长
    // 格式化时间（以秒为单位，mm:ss 格式）
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

        // 清除所有现存字幕
        subtitleClearTimer->stop();
        subtitleLabel->hide();
        verticalClearTimer->stop();
        verticalSubtitleLabel->hide();
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
        playButton->setIcon(QIcon(":/icons/pause"));
        playButton->setEnabled(true);
    }
}

void MediaWindow::stopPlay()
{
    // 停 signal/slot，通知 worker 停掉循环
    if (demuxWorker) {
        disconnect(demuxWorker, nullptr, this, nullptr);
        // 通知 worker 停止播放
        demuxWorker->isPlay = false;
        // 如果处于暂停状态，先唤醒，防止线程阻塞
        demuxWorker->pauseCondition.wakeAll();
    }

    // 请求线程中断并结束事件循环
    if (demuxThread) {
        demuxThread->requestInterruption();
        demuxThread->quit();
        demuxThread->wait();
    }


    delete demuxWorker;
    demuxWorker = nullptr;
    qDebug() << "解码器已释放";
    delete demuxThread;
    demuxThread = nullptr;
    qDebug() << "解码进程已退出";

    // 恢复按钮状态
    playButton->setEnabled(true);
    playButton->setIcon(QIcon(":/icons/play"));

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

    return 0;
}

void MediaWindow::onVideoListDoubleClicked(QListWidgetItem *item)
{
    if(!item) return;
    QString newFilePath = item->data(Qt::UserRole).toString();
    if (newFilePath.isEmpty() || newFilePath == selectedFilePath) return;

    // 停止当前播放
    stopPlay();
    loadFile(newFilePath);

}

void MediaWindow::onDeepSeekButtonClicked()
{
    QString query = deepSeekLineEdit->text().trimmed();
    if(query.isEmpty()) {
        deepSeekResultText->setText("请输入查询内容。");
        return;
    }

    // 构造 DeepSeek API 请求
    QUrl url("https://api.deepseek.com/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 添加授权头部
    request.setRawHeader("Authorization", "Bearer sk-350bb32b02434688add2b505e5eb5eb0");

    // 构造 JSON 请求体
    // 系统信息
    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = "You are a helpful assistant.";
    // 用户信息
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

    // 发起 POST 请求
    QNetworkReply *reply = m_networkManager->post(request, postData);

    // 当请求完成后解析响应
    connect(reply, &QNetworkReply::finished, [=](){
        if(reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            qDebug() << "DeepSeek API 返回:" << response;
            QJsonDocument responseDoc = QJsonDocument::fromJson(response);
            qDebug() << "1";
            if(responseDoc.isNull() || !responseDoc.isObject()){
                deepSeekResultText->setText("解析返回结果失败。返回内容: " + response);
                qDebug() << "2";
            } else {
                QJsonObject responseObj = responseDoc.object();
                QString answer;
                qDebug() << "3";
                if(responseObj.contains("choices")){
                    QJsonArray choices = responseObj.value("choices").toArray();
                    qDebug() << "4";
                    if(!choices.isEmpty()){
                        QJsonObject firstChoice = choices.first().toObject();
                        qDebug() << "5";
                        if(firstChoice.contains("message")){
                            QJsonObject messageObj = firstChoice.value("message").toObject();
                            answer = messageObj.value("content").toString();
                            qDebug() << "6";
                        }
                    }
                }
                qDebug() << "7";
                if(answer.isEmpty()){
                    answer = "未返回答案，请检查查询参数或 API 状态。";
                    qDebug() << "8";
                }
                deepSeekResultText->setText(answer);
                qDebug() << "9";
            }
        } else {
            qDebug() << "DeepSeek 请求错误:" << reply->errorString();
            deepSeekResultText->setText("请求失败: " + reply->errorString());
        }
        qDebug() << "10";
        reply->deleteLater();
        // manager->deleteLater();
        qDebug() << "11";
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
        QListWidgetItem *item = new QListWidgetItem(fi.fileName(), videoListWidget);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        videoListWidget->addItem(item);
    }
}

void MediaWindow::onSelectStreamButtonClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("选择流"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QComboBox *videoCombo = new QComboBox(&dialog);
    QComboBox *audioCombo = new QComboBox(&dialog);
    QComboBox *subtitleCombo = new QComboBox(&dialog);

    // 利用 FFmpeg 打开当前文件，并获取流信息（注意释放资源）
    AVFormatContext *fmtCtx = nullptr;
    QByteArray filePathBytes = selectedFilePath.toLocal8Bit();
    if(avformat_open_input(&fmtCtx, filePathBytes.data(), nullptr, nullptr) != 0) {
        QMessageBox::warning(this, tr("错误"), tr("无法打开文件获取流信息。"));
        return;
    }
    if(avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        QMessageBox::warning(this, tr("错误"), tr("无法获取流信息。"));
        avformat_close_input(&fmtCtx);
        return;
    }
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        AVStream *stream = fmtCtx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoCombo->addItem(getStreamDescription(stream), i);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioCombo->addItem(getStreamDescription(stream), i);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            subtitleCombo->addItem(getStreamDescription(stream), i);
        }
    }
    avformat_close_input(&fmtCtx);

    layout->addWidget(new QLabel(tr("视频流："), &dialog));
    layout->addWidget(videoCombo);
    layout->addWidget(new QLabel(tr("音频流："), &dialog));
    layout->addWidget(audioCombo);
    layout->addWidget(new QLabel(tr("字幕流："), &dialog));
    layout->addWidget(subtitleCombo);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        selectedVideoStream = videoCombo->currentData().toInt();
        selectedAudioStream = audioCombo->currentData().toInt();
        selectedSubtitleStream = subtitleCombo->currentData().toInt();
        // 重启播放
        restartPlaybackAfterStreamSelection();
    }
}

void MediaWindow::slotDisplaySubtitle(const QString &subtitleText, int durationMs)
{
    // 如果空文本，则立刻清除并隐藏
    if (subtitleText.isEmpty()) {
        subtitleLabel->clear();
        subtitleLabel->hide();
        subtitleClearTimer->stop();
        return;
    }

    // 去掉 ASS 样式
    QString clean = subtitleText;
    clean.remove(QRegularExpression("\\{[^}]*\\}"));

    // 把旧的 pixmap 清掉，切换到文本模式
    subtitleLabel->setPixmap(QPixmap());

    // 设置文字和样式
    subtitleLabel->setWordWrap(true);

    // 创建一个阴影效果对象
    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect(subtitleLabel);
    shadowEffect->setOffset(0, 0);             // 设置阴影偏移为零（正中央）
    shadowEffect->setBlurRadius(8);            // 设置模糊半径
    shadowEffect->setColor(Qt::black);         // 设置阴影颜色为黑色

    // 应用阴影效果到 subtitleLabel
    subtitleLabel->setGraphicsEffect(shadowEffect);

    subtitleLabel->setText(clean);

    // 动态测算：宽度最多占容器 80%
    QWidget *overlay = videoFrame->layout()->itemAt(0)->widget();
    int cw = overlay->width();
    int ch = overlay->height();
    int maxW = int(cw * 0.8);

    QFont font = subtitleLabel->font();
    QFontMetrics fm(font);
    // 这里第二个参数给一个足够大的高度，word-wrap 会自动换行
    QRect br = fm.boundingRect(0, 0, maxW, ch,
                               Qt::AlignCenter | Qt::TextWordWrap,
                               clean);

    // 加点内边距
    int pad = 10;
    int w = br.width() + pad * 2;
    int h = br.height() + pad * 2;

    // 放到底部居中，距底部 10px
    int x = (cw - w) / 2;
    int y = ch - h - 10;
    subtitleLabel->setGeometry(x, y, w, h);

    // 显示
    subtitleLabel->show();
    subtitleLabel->raise();

    // 设定延时自动清除字幕显示
    subtitleClearTimer->stop();
    subtitleClearTimer->start(durationMs);
}

void MediaWindow::slotDisplaySubtitleImage(const QImage &subtitleImage, int durationMs)
{
    if (subtitleImage.isNull())
        return;

    // overlay 容器大小
    QWidget *overlay = videoFrame->layout()->itemAt(0)->widget();
    int containerW = overlay->width();
    int containerH = overlay->height();

    // 原始字幕图片尺寸
    int origW = subtitleImage.width();
    int origH = subtitleImage.height();

    // 判断竖排：只有当高度明显大于宽度时才视为竖排
    constexpr double verticalThreshold = 1.2;
    bool isVertical = (origH > origW * verticalThreshold);

    QPixmap pix;
    if (isVertical) {
        // —— 竖排处理：占用 80% 高度，5% 宽度，右侧居中

        // 清除竖排旧字幕
        verticalSubtitleLabel->hide();
        verticalSubtitleLabel->clear();
        verticalClearTimer->stop();

        constexpr double maxHRatio = 0.8;
        constexpr double maxWRatio = 0.05;
        int maxH = int(containerH * maxHRatio);
        int maxW = int(containerW * maxWRatio);

        QSize targetSize = QSize(origW, origH)
                               .scaled(maxW, maxH, Qt::KeepAspectRatio);

        pix = QPixmap::fromImage(
            subtitleImage.scaled(
                targetSize,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
                )
            );

        // 右侧居中放置，距右边留 10px
        int margin = 50;
        int x = containerW - pix.width() - margin;
        int y = (containerH - pix.height()) / 2;

        verticalSubtitleLabel->setGeometry(x, y, pix.width(), pix.height());
        verticalSubtitleLabel->setPixmap(pix);
        verticalSubtitleLabel->setScaledContents(false);
        verticalSubtitleLabel->show();
        verticalSubtitleLabel->raise();

        // 延时自动清除竖排字幕
        verticalClearTimer->start(durationMs);
    }
    else {
        // 横排处理：占用 80% 宽度，10% 高度，底部居中

        // 清除横排旧字幕
        subtitleLabel->hide();
        subtitleLabel->clear();
        subtitleClearTimer->stop();

        // 全局缩放比例（基于原始视频宽度）
        double globalScale = double(containerW) / videoWidth;

        // 先按全局缩放，只缩小不放大
        int targetW = qMin(int(origW * globalScale), origW);
        int targetH = qMin(int(origH * globalScale), origH);

        // 宽度不超 80%
        constexpr double maxWidthRatio = 0.85;
        int maxW = int(containerW * maxWidthRatio);
        if (targetW > maxW) {
            double r = double(maxW) / targetW;
            targetW = int(targetW * r);
            targetH = int(targetH * r);
        }

        // 高度不超 10%
        constexpr double maxHeightRatio = 0.15;
        int maxH = int(containerH * maxHeightRatio);
        if (targetH > maxH) {
            double r = double(maxH) / targetH;
            targetH = int(targetH * r);
            targetW = int(targetW * r);
        }

        pix = QPixmap::fromImage(
            subtitleImage.scaled(
                QSize(targetW, targetH),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
                )
            );

        // 底部居中，距底部 10px
        int x = (containerW - pix.width()) / 2;
        int y = containerH - pix.height() - 20;
        subtitleLabel->setGeometry(x, y, pix.width(), pix.height());
        subtitleLabel->setPixmap(pix);
        subtitleLabel->setScaledContents(false);
        subtitleLabel->show();
        subtitleLabel->raise();

        // 重置横排定时
        subtitleClearTimer->start(durationMs);
    }
}

void MediaWindow::onLoadSubtitleButtonClicked()
{
    // 默认路径：跟视频文件同目录
    QString dir = QFileInfo(selectedFilePath).absolutePath();
    QString filter = tr("ASS Subtitles (*.ass *.ssa)");
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("加载外挂字幕"),
        dir,
        filter
        );
    if (path.isEmpty())
        return;

    externalSubtitlePath = path;
    qDebug() << "选中外挂字幕：" << externalSubtitlePath;

    emit sigLoadExternalSubtitle(externalSubtitlePath);

}

void MediaWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        // 恢复窗口装饰
        showNormal();
        titleBar->show();
        controlFrame->show();
        rightTabWidget->show();
    } else {
        // 进入纯视频全屏
        showFullScreen();
        titleBar->hide();
        controlFrame->hide();
        rightTabWidget->hide();
    }
}

void MediaWindow::setupUI()
{
    // 创建主布局 Splitter
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    leftSplitter = new QSplitter(Qt::Vertical);

    // 创建一个专门的 QWidget 用作视频和字幕叠加的容器
    overlayContainer = new QWidget(this);
    overlayContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QStackedLayout *stackedLayout = new QStackedLayout(overlayContainer);
    // 设置叠加模式，让所有子控件同时显示
    stackedLayout->setStackingMode(QStackedLayout::StackAll);

    // 视频显示区域
#ifdef Q_OS_WIN
    // 创建 CudaVideoWindow
    cudaVideoWindow = new CudaVideoWindow(overlayContainer);
    // 添加视频 widget 到叠加布局底层
    stackedLayout->addWidget(cudaVideoWindow);
#else
    videoLabel = new QLabel(overlayContainer);
    videoLabel->setAttribute(Qt::WA_NativeWindow);
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    videoLabel->setStyleSheet("background-color: black;");
    stackedLayout->addWidget(videoLabel);
#endif

#ifdef Q_OS_WIN
    QFont initFont("Arial", 20);  // 默认 20pt
#elif defined(Q_OS_MAC)
    QFont initFont("Arial", 38);
#endif
    // 创建横排字幕 Label
    subtitleLabel = new QLabel(overlayContainer);
    subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    subtitleLabel->setStyleSheet("background-color: transparent; color: white;");
    subtitleLabel->setFont(initFont);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setParent(overlayContainer);
    subtitleLabel->raise();
    subtitleLabel->hide();

    // 创建竖排字幕 Label
    verticalSubtitleLabel = new QLabel(overlayContainer);
    verticalSubtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    verticalSubtitleLabel->setStyleSheet("background-color: transparent; color: white;");
    verticalSubtitleLabel->setFont(initFont);
    verticalSubtitleLabel->setParent(overlayContainer);
    verticalSubtitleLabel->hide();
    verticalSubtitleLabel->raise();

    // 创建 videoFrame，将 overlayContainer 放入其中
    videoFrame = new QFrame();
    QVBoxLayout *frameLayout = new QVBoxLayout(videoFrame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->addWidget(overlayContainer);
    overlayContainer->installEventFilter(this);

    // 控制面板区域
    controlFrame = new QFrame();
    controlFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    controlFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // 控制面板
    setupControlPanel();

    // 创建右侧区域使用 QTabWidget
    rightTabWidget = new QTabWidget(this);
    rightTabWidget->setAttribute(Qt::WA_StyledBackground, true);
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

    // 组装主界面：左侧为视频+控制，右侧为其他页面
    leftSplitter->addWidget(videoFrame);
    leftSplitter->addWidget(controlFrame);
    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(rightTabWidget);
    // 设置 Splitter 拉伸
    leftSplitter->setStretchFactor(0, 10);
    leftSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(0, 8);
    mainSplitter->setStretchFactor(1, 2);

    // 主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(titleBar);
    mainLayout->addWidget(mainSplitter);
    setLayout(mainLayout);
    this->setMinimumSize(800, 600);
}

void MediaWindow::setupConnections()
{
    // 接收获取视频尺寸
    connect(demuxWorker, &DemuxWorker::sigGetVideoInfo, this, &MediaWindow::initVideoDisplay);
    // 接收视频帧并渲染
#ifdef Q_OS_WIN
    connect(demuxWorker, &DemuxWorker::sigGetFrame, this, &MediaWindow::updateCudaVideo);
#else
    connect(demuxWorker, &DemuxWorker::sigGetFrame, this, &MediaWindow::updateVideo);
#endif
    // 发送播放信号
    connect(this, &MediaWindow::sigStartPlay, demuxWorker, &DemuxWorker::slotDoWork);
    // 发送文件路径
    connect(this, &MediaWindow::sigSetFilePath, demuxWorker, &DemuxWorker::setFilePath);
    // 接收播放时间
    connect(demuxWorker, &DemuxWorker::sigUpdateTime, this, &MediaWindow::updateTimeLabel);
    // 接收编解码器信息
    connect(demuxWorker, &DemuxWorker::sigCodecInfo, this, &MediaWindow::updateCodecInfo);
    // 发送跳转目标时间
    connect(this, &MediaWindow::sigSeekTo, demuxWorker, &DemuxWorker::seekTo, Qt::QueuedConnection);
    // 发送相对跳转时间
    connect(this, &MediaWindow::sigSeekRelative, demuxWorker, &DemuxWorker::seekRelative, Qt::QueuedConnection);
    // 发送声音大小
    connect(this, &MediaWindow::sigSetVolume, demuxWorker, &DemuxWorker::setVolume, Qt::QueuedConnection);
    // 接收文字字幕
    connect(demuxWorker, &DemuxWorker::sigSubtitle, this, &MediaWindow::slotDisplaySubtitle);
    // 接收图片字幕
    connect(demuxWorker, &DemuxWorker::sigSubtitleImage, this, &MediaWindow::slotDisplaySubtitleImage);
    // 发送外挂字幕路径
    connect(this, &MediaWindow::sigLoadExternalSubtitle,
            demuxWorker, &DemuxWorker::setExternalSubtitle);
    // 由控制面板发送倍速信息
    connect(controlDialog, &ControlPanelDialog::setPlaybackSpeed,
            demuxWorker,   &DemuxWorker::setPlaybackSpeed, Qt::QueuedConnection);
}

void MediaWindow::restartPlaybackAfterStreamSelection()
{
    stopPlay();
    // 建立新线程和 worker
    demuxThread = new QThread;
    demuxWorker = new DemuxWorker;
    demuxWorker->moveToThread(demuxThread);
    setupConnections(); // 重新建立所有信号连接
    // 将新的流选择参数传递给新创建的 demuxWorker
    demuxWorker->setStreamIndices(selectedVideoStream, selectedAudioStream, selectedSubtitleStream);
    // 传入当前选择的文件路径
    emit sigSetFilePath(selectedFilePath);
    // 若需要，可以在此处传递当前播放位置（例如调用 seekTo(currentTime)）
    startPlay();
}

QString MediaWindow::getStreamDescription(AVStream *stream)
{
    QString desc = QString(tr("索引 %1: ")).arg(stream->index);
    AVCodecParameters *par = stream->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        desc += tr("视频");
        desc += QString(" (%1 x %2)").arg(stream->codecpar->width).arg(stream->codecpar->height);
        // 可以读取附加信息，例如 codec id 的名称、可能的帧率等
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if(codec)
            desc += QString(" - %1").arg(codec->long_name);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        desc += tr("音频");
        // 采样率、通道数、编码器名称等
        desc += QString(tr(" - %1 Hz, %2通道")).arg(par->sample_rate).arg(par->ch_layout.nb_channels);
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if(codec)
            desc += QString(" - %1").arg(codec->long_name);
    } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        desc += tr("字幕");
        // 可以从 metadata 中提取语言信息
        AVDictionaryEntry *langEntry = av_dict_get(stream->metadata, "language", nullptr, 0);
        if(langEntry)
            desc += QString(" (%1)").arg(langEntry->value);
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if(codec)
            desc += QString(" - %1").arg(codec->long_name);
    }
    return desc;

}

void MediaWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 假设 videoFrame 的布局中只有一个 overlayContainer
    if (!subtitleLabel->pixmap().isNull()) {
        QWidget *overlay = videoFrame->layout()->itemAt(0)->widget();
        int containerW = overlay->width();
        int containerH = overlay->height();
        const QPixmap &pix = subtitleLabel->pixmap();
        int w = pix.width();
        int h = pix.height();

        int x = (containerW - w) / 2;
        int y = containerH - h - 10;
        subtitleLabel->setGeometry(x, y, w, h);
    }
}

bool MediaWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理 overlayContainer 的双击
    if (watched == overlayContainer && event->type() == QEvent::MouseButtonDblClick) {
        toggleFullScreen();
        return true;   // 表示已处理
    }
    return QWidget::eventFilter(watched, event);
}

void MediaWindow::loadFile(const QString &filePath)
{
    selectedFilePath = filePath;
    setWindowTitle(tr("Media Player - %1").arg(QFileInfo(filePath).fileName()));

    // 4) 创建新线程和 worker，connect 信号，然后 start()
    demuxThread = new QThread(this);
    demuxWorker = new DemuxWorker;
    demuxWorker->moveToThread(demuxThread);

    // 线程启动后调用解复用
    connect(demuxThread, &QThread::started,
            demuxWorker, &DemuxWorker::slotDoWork, Qt::QueuedConnection);

    // 解复用完成后自动删自己、删线程
    connect(demuxWorker, &DemuxWorker::finished,
            demuxThread,   &QThread::quit, Qt::QueuedConnection);
    connect(demuxThread, &QThread::finished,
            demuxThread,   &QObject::deleteLater, Qt::QueuedConnection);
    connect(demuxWorker, &DemuxWorker::finished,
            demuxWorker,   &QObject::deleteLater, Qt::QueuedConnection);

    setupConnections();
    emit sigSetFilePath(selectedFilePath);

    // 自动寻找 .chs.ass 或 .cht.ass
    QString base = QFileInfo(selectedFilePath).absolutePath() + "/" + QFileInfo(selectedFilePath).completeBaseName();
    QString chs = base + ".chs.ass";
    QString cht = base + ".cht.ass";

    if (QFile::exists(chs)) {
        externalSubtitlePath = chs;
        emit sigLoadExternalSubtitle(externalSubtitlePath);
        qDebug() << "自动加载简体字幕：" << externalSubtitlePath;
    }
    else if (QFile::exists(cht)) {
        externalSubtitlePath = cht;
        emit sigLoadExternalSubtitle(externalSubtitlePath);
        qDebug() << "自动加载繁体字幕：" << externalSubtitlePath;
    }

    startPlay();
}

void MediaWindow::setupControlPanel()
{
    QVBoxLayout *controlLayout = new QVBoxLayout(controlFrame);
    controlLayout->setContentsMargins(5, 5, 5, 5);
    controlLayout->setSpacing(5);

    // 第一排：进度条 + 音量
    QHBoxLayout *row1Layout = new QHBoxLayout();
    progressSlider = createSlider(0, 1000, 0);
    volumeSlider = createSlider(0, 100, 100);

    row1Layout->addWidget(progressSlider, 1);
    row1Layout->addWidget(new QLabel(tr("Volume"), controlFrame));
    row1Layout->addWidget(volumeSlider, 0);

    controlLayout->addLayout(row1Layout);

    // 第二排：按钮
    QHBoxLayout *row2Layout = new QHBoxLayout();
    openButton = new QPushButton(QIcon(":/icons/folder"), "", this);
    playButton = new QPushButton(QIcon(":/icons/play"), "", this);
    stopButton = new QPushButton(QIcon(":/icons/stop"), "", this);
    rewindButton = new QPushButton(QIcon(":/icons/rewind"), "", this);
    forwardButton = new QPushButton(QIcon(":/icons/forward"), "", this);

    openButton->setObjectName("openButton");
    openButton->setIconSize(QSize(32,32));
    openButton->setToolTip(tr("Open File"));

    playButton->setObjectName("playButton");
    playButton->setIconSize(QSize(32,32));
    playButton->setToolTip(tr("Play"));
    playButton->setEnabled(false);

    stopButton->setObjectName("stopButton");
    stopButton->setIconSize(QSize(32,32));
    stopButton->setToolTip(tr("Stop"));

    rewindButton->setObjectName("rewindButton");
    rewindButton->setIconSize(QSize(32,32));
    rewindButton->setToolTip(tr("rewind"));

    forwardButton->setObjectName("forwardButton");
    forwardButton->setIconSize(QSize(32,32));
    forwardButton->setToolTip(tr("forward"));

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
    toggleControlPanelButton = new QPushButton(QIcon(":/icons/general"),"",  controlFrame);
    toggleControlPanelButton->setObjectName("toggleControlPanelButton");
    toggleControlPanelButton->setIconSize(QSize(32,32));
    toggleControlPanelButton->setToolTip(tr("Control Panel"));

    row2Layout->addWidget(toggleControlPanelButton);

    controlLayout->addLayout(row2Layout);
}

QSlider *MediaWindow::createSlider(int min, int max, int value)
{
    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(value);
    slider->setTracking(true);  // 确保拖动时能发送信号
    return slider;
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
    // 原始尺寸 videoWidth x videoHeight，目标尺寸为 targetW x targetH，转换格式为 RGB24
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

    // 进行颜色空间转换并缩放到目标尺寸
    sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, videoHeight,
              pFrameRGB->data, pFrameRGB->linesize);

    QImage image(pFrameRGB->data[0], targetW, targetH, pFrameRGB->linesize[0], QImage::Format_RGB888);
    QImage imageCopy = image.copy();

    // 释放资源
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrame);

    // 在 GUI 中更新 videoLabel
    QMetaObject::invokeMethod(videoLabel, [=]() {
            videoLabel->setPixmap(QPixmap::fromImage(imageCopy));
        }, Qt::QueuedConnection);
}
#endif
