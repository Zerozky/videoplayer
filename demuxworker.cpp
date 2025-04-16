#include "demuxworker.h"
#include <QDebug>
#include <QLabel>
#include <QTime>
#include <QCoreApplication>
#include <sys/stat.h>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QElapsedTimer>
#include <QVideoSink>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

// 硬件像素格式选择回调函数
#if defined(Q_OS_WIN)
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    // NVDEC 使用的硬件像素格式
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_CUDA)
            return *p;
    }
    qDebug() << "Failed to get HW surface format for NVDEC.";
    return pix_fmts[0];
}
#elif defined(Q_OS_MAC)
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    // VideoToolbox 使用的硬件像素格式
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX)
            return *p;
    }
    qDebug() << "Failed to get HW surface format for VideoToolbox.";
    return pix_fmts[0];
}
#endif

static int interrupt_callback(void *ctx)
{
    DemuxWorker *worker = reinterpret_cast<DemuxWorker*>(ctx);
    // 当 isPlay 为 false 时中断阻塞调用
    return (worker->isPlay ? 0 : 1);
}

DemuxWorker::DemuxWorker(QObject *parent):QObject (parent)
{
    qDebug()<<"Thread构造函数ID:"<<QThread::currentThreadId();
    filePath = "";
}

void DemuxWorker::setStreamIndices(int videoIdx, int audioIdx, int subtitleIdx)
{
    selectedVideoStream = videoIdx;
    selectedAudioStream = audioIdx;
    selectedSubtitleStream = subtitleIdx;
}

double DemuxWorker::getcurrentPlaybackTime()
{
    return currentPlaybackTime;
}

DemuxWorker::~DemuxWorker()
{

    qDebug() << "thread quit";
}

void DemuxWorker::setFilePath(const QString &path)
{
    filePath = path;
    qDebug() << "File path set to:" << filePath;
}

void DemuxWorker::seekTo(double newTime)
{
    QMutexLocker locker(&seekMutex);
    pendingSeekTime = newTime;
}

void DemuxWorker::seekRelative(double offset)
{
    QMutexLocker locker(&seekMutex);
    pendingSeekOffset += offset;
}

void DemuxWorker::setVolume(double volume)
{
    if(m_audioSink)
        m_audioSink->setVolume(volume);
}

void DemuxWorker::slotDoWork()
{
    if (filePath.isEmpty())
    {
        qDebug() << "No file selected";
        return;
    }

    // 声明所有需要释放的资源指针
    AVFormatContext *avFormatContext = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *pFrame = nullptr;
    AVCodecContext *pVideoCodecContext = nullptr;
    AVCodecContext *pAudioCodecContext = nullptr;
    AVCodecContext *pSubtitleCodecContext = nullptr;
    SwrContext *swrCtx = nullptr;
    QAudioSink *audioSink = nullptr;
    QIODevice *audioDevice = nullptr;

    // 定义局部 lambda 统一清理
    auto cleanup = [&]() {
        if (audioSink) {
            audioSink->stop();
            delete audioSink;
            audioSink = nullptr;
        }
        if (pVideoCodecContext) {
            avcodec_free_context(&pVideoCodecContext);
        }
        if (pAudioCodecContext) {
            avcodec_free_context(&pAudioCodecContext);
        }
        if (pSubtitleCodecContext) {
            avcodec_free_context(&pSubtitleCodecContext);
        }
        if (swrCtx) {
            swr_free(&swrCtx);
        }
        if (avFormatContext) {
            avformat_close_input(&avFormatContext);
            qDebug() << "end of play";
        }
        if (packet) {
            av_packet_free(&packet);
        }
        if (pFrame) {
            av_frame_free(&pFrame);
        }
    };

    // 1.分配解复用器上下文
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext)
    {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate context.\n");
        cleanup();
        return;
    }

    // 设置中断回调
    avFormatContext->interrupt_callback.callback = interrupt_callback;
    avFormatContext->interrupt_callback.opaque = this;

    packet = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (!packet || !pFrame)
    {
        qDebug() << "Failed to allocate packet or frame";
        cleanup();
        return;
    }

    // 如果用户没有选择特定的流，则默认值为 -1
    int videoStream = (selectedVideoStream >= 0 ? selectedVideoStream : -1);
    int audioStream = (selectedAudioStream >= 0 ? selectedAudioStream : -1);
    int subtitleStream = (selectedSubtitleStream >= 0 ? selectedSubtitleStream : -1);
    int err = -1;

    QByteArray filePathBytes = filePath.toLocal8Bit();
    char *fileName = filePathBytes.data();

    // 标记播放状态
    isPlay = true;
    isPause = false;

    // 2.根据URL打开本地文件或网络流
    if(avformat_open_input(&avFormatContext, fileName, nullptr, nullptr) != 0)
    {
        qDebug() << "Couldn't open file";
        cleanup();
        return;
    }

    // 3.读取媒体的部分数据包以获取码流信息
    if(avformat_find_stream_info(avFormatContext, nullptr) < 0)
    {
        qDebug() << "Couldn't find stream information";
        cleanup();
        return;
    }

    // 计算总时长（单位：秒）
    double totalDuration = (avFormatContext->duration > 0)
                               ? avFormatContext->duration / static_cast<double>(AV_TIME_BASE)
                               : 0;

    // 查找音视频流索引
    for (unsigned int i = 0; i < avFormatContext->nb_streams; i++) {
        AVMediaType type = avFormatContext->streams[i]->codecpar->codec_type;
        if(type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
            videoStream = i;
        else if(type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
            audioStream = i;
        else if(type == AVMEDIA_TYPE_SUBTITLE && subtitleStream < 0)
            subtitleStream = i;
    }
    if(audioStream == -1 && videoStream == -1)
    {
        qDebug() << "Didn't find a valid stream";
        cleanup();
        return;
    }

    // 字幕解码器初始化
    if (subtitleStream != -1) {
        AVCodecParameters *pSubtitleCodecPara = avFormatContext->streams[subtitleStream]->codecpar;
        pSubtitleCodecContext = avcodec_alloc_context3(nullptr);
        if (!pSubtitleCodecContext) {
            qDebug() << "avcodec_alloc_context3 for subtitle failed";
        } else if (avcodec_parameters_to_context(pSubtitleCodecContext, pSubtitleCodecPara) < 0) {
            qDebug() << "avcodec_parameters_to_context for subtitle failed";
            avcodec_free_context(&pSubtitleCodecContext);
            pSubtitleCodecContext = nullptr;
        } else {
            const AVCodec *pCodecSubtitle = avcodec_find_decoder(pSubtitleCodecPara->codec_id);
            if (!pCodecSubtitle) {
                qDebug() << "avcodec_find_decoder for subtitle failed";
                avcodec_free_context(&pSubtitleCodecContext);
                pSubtitleCodecContext = nullptr;
            } else if (avcodec_open2(pSubtitleCodecContext, pCodecSubtitle, nullptr) < 0) {
                qDebug() << "avcodec_open2 for subtitle failed";
                avcodec_free_context(&pSubtitleCodecContext);
                pSubtitleCodecContext = nullptr;
            }
        }
    }

    // 初始化视频解码器
    const AVCodec   *pCodecVideo = nullptr;
    AVCodecParameters *pVideoCodecPara = nullptr;

    if(videoStream != -1)
    {
        pVideoCodecPara = avFormatContext->streams[videoStream]->codecpar;
        // 1.分配编解码器上下文
        pVideoCodecContext =  avcodec_alloc_context3(nullptr);
        if (!pVideoCodecContext){
            qDebug() <<  "avcodec_alloc_context3 failed";
            cleanup();
            return;
        }

        // 2.将码流中的编解码器信息拷贝到AVCodecContext
        err = avcodec_parameters_to_context(pVideoCodecContext, pVideoCodecPara);
        if (err < 0){
            qDebug() << "avcodec_parameters_to_context failed";
            cleanup();
            return;
        }

        // 设置硬件设备上下文
#if defined(Q_OS_WIN)
        {
            AVBufferRef *hw_device_ctx = nullptr;
            err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
            if (err < 0) {
                qDebug() << "Failed to create CUDA device context for NVDEC, error:" << err;
                // 回退到纯软件解码
            } else {
                pVideoCodecContext->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                // 释放原始引用
                av_buffer_unref(&hw_device_ctx);
            }
        }
#elif defined(Q_OS_MAC)
        {
            AVBufferRef *hw_device_ctx = nullptr;
            err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
            if (err < 0) {
                qDebug() << "Failed to create VideoToolbox device context, error:" << err;
                // 回退到纯软件解码
            } else {
                pVideoCodecContext->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                // 释放原始引用
                av_buffer_unref(&hw_device_ctx);
            }
        }
#endif
        // 设置选择硬件像素格式的回调函数
        pVideoCodecContext->get_format = get_hw_format;

        // 3.根据编解码器信息查找相应的解码器
        pCodecVideo = avcodec_find_decoder(pVideoCodecPara->codec_id);
        if(pCodecVideo == nullptr) {
            qDebug() <<  "avcodec_find_decoder failed";
            cleanup();
            return;
        }
        qDebug() << "视频编解码器名:" << pCodecVideo->long_name;

        // 4.打开编解码器并关联到AVCodecContext
        err = avcodec_open2(pVideoCodecContext, pCodecVideo, nullptr);
        if(err){
            qDebug() << "avcodec_open2 failed";
            cleanup();
            return;
        }
        emit sigGetVideoInfo(pVideoCodecContext->width, pVideoCodecContext->height);
        qDebug() << "视频宽度:" << pVideoCodecContext->width << "高度:" << pVideoCodecContext->height;
    }

    // 初始化音频解码器
    if (audioStream != -1) {
        AVCodecParameters *pAudioCodecPara = avFormatContext->streams[audioStream]->codecpar;

        pAudioCodecContext = avcodec_alloc_context3(nullptr);
        if (!pAudioCodecContext) {
            qDebug() << "avcodec_alloc_context3 for audio failed";
            cleanup();
            return;
        }

        if (avcodec_parameters_to_context(pAudioCodecContext, pAudioCodecPara) < 0) {
            qDebug() << "avcodec_parameters_to_context for audio failed";
            cleanup();
            return;
        }

        const AVCodec *pCodecAudio = avcodec_find_decoder(pAudioCodecPara->codec_id);
        if (!pCodecAudio) {
            qDebug() << "avcodec_find_decoder for audio failed";
            cleanup();
            return;
        }

        if (avcodec_open2(pAudioCodecContext, pCodecAudio, nullptr) < 0) {
            qDebug() << "avcodec_open2 for audio failed";
            cleanup();
            return;
        }

        // 获取默认音频设备
        QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();

        // 设置 Qt 音频格式
        QAudioFormat format;
        format.setSampleRate(pAudioCodecContext->sample_rate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);

        // 初始化 QAudioSink
        audioSink = new QAudioSink(defaultDevice, format);
        audioSink->setVolume(1.0);
        // 设置较大的缓冲区
        audioSink->setBufferSize(100 * 1024);
        // 获取 QIODevice 用于写入音频数据
        audioDevice = audioSink->start();
        m_audioSink = audioSink;
        // 初始化 SwrContext（音频重采样）
        swrCtx = swr_alloc();
        if (!swrCtx) {
            qDebug() << "swr_alloc failed";
            cleanup();
            return;
        }

        // 使用 AVChannelLayout 代替 channel_layout
        AVChannelLayout in_ch_layout = pAudioCodecPara->ch_layout;

        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);  // 输出设为立体声

        av_opt_set_chlayout(swrCtx, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_chlayout(swrCtx, "out_chlayout", &out_ch_layout, 0);

        av_opt_set_int(swrCtx, "in_sample_rate", pAudioCodecPara->sample_rate, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", pAudioCodecPara->sample_rate, 0);

        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", (AVSampleFormat)pAudioCodecPara->format, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        if (swr_init(swrCtx) < 0) {
            qDebug() << "无法初始化 SwrContext";
            cleanup();
            return;
        }
    }

    m_avFormatContext = avFormatContext;
    m_videoCodecContext = pVideoCodecContext;
    m_audioCodecContext = pAudioCodecContext;

    // 发射编解码器信息信号
    QString codecInfo;
    if(pCodecVideo) {
        codecInfo += QString("%1/").arg(pCodecVideo->name).toUpper();
    }
    if(audioStream != -1) {
        const AVCodec *pCodecAudio = avcodec_find_decoder(avFormatContext->streams[audioStream]->codecpar->codec_id);
        if(pCodecAudio)
            codecInfo += QString("%1").arg(pCodecAudio->name).toUpper();
    }
    emit sigCodecInfo(codecInfo);

    // 记录开始播放时间和暂停累计时间
    qint64 startTime = 0;
    qint64 pauseTotal = 0;
    qint64 pauseStart = 0;
    QElapsedTimer timer;
    timer.start();
    startTime = timer.elapsed();  // 初始播放时间

    while(isPlay) {
        // 检查跳转请求
        {
            QMutexLocker locker(&seekMutex);
            if (pendingSeekTime >= 0 || pendingSeekOffset != 0) {
                double referenceTime = currentPlaybackTime;  // 使用当前播放时间作为参考
                double targetTime = (pendingSeekTime >= 0) ? pendingSeekTime : (referenceTime + pendingSeekOffset);
                int64_t timestamp = static_cast<int64_t>(targetTime * AV_TIME_BASE);
                if(avformat_seek_file(avFormatContext, -1, INT64_MIN, timestamp, INT64_MAX, 0) < 0) {
                    qDebug() << "Seek failed!";
                } else {
                    if (pVideoCodecContext)
                        avcodec_flush_buffers(pVideoCodecContext);
                    if (pAudioCodecContext)
                        avcodec_flush_buffers(pAudioCodecContext);
                    // 重置计时器，使 effectiveTime 立刻匹配 targetTime：
                    startTime = timer.elapsed() - static_cast<qint64>(targetTime * 1000);
                    pauseTotal = 0;
                    currentPlaybackTime = targetTime;
                }
                pendingSeekTime = -1;
                pendingSeekOffset = 0;
            }
        }

        // 如果暂停，则进入阻塞等待状态
        pauseMutex.lock();
        while(isPause && isPlay) {
            if (pauseStart == 0) {
                pauseStart = timer.elapsed();
            }
            // 等待100ms，便于周期性检查 isPlay
            pauseCondition.wait(&pauseMutex, 100);
        }
        // 如果刚从暂停恢复，则累计暂停时长
        if (!isPause && pauseStart != 0) {
            pauseTotal += (timer.elapsed() - pauseStart);
            pauseStart = 0;
        }
        pauseMutex.unlock();

        // 检查停止标志
        if (!isPlay) {
            break;
        }

        // 4.从文件中获取数据包
        if(av_read_frame(avFormatContext, packet) < 0) {
            break;
        }

        // 如果在处理数据包过程中被设置为停止，则跳出循环
        if (!isPlay) {
            av_packet_unref(packet);
            break;
        }

        // 处理视频帧
        if(packet->stream_index == videoStream)
        {
            // 5.1向解码器发送数据包
            err = avcodec_send_packet(pVideoCodecContext, packet);
            if(err != 0) {
                if (AVERROR(EAGAIN) == err) {
                    av_packet_unref(packet);
                    continue;
                }
                qDebug() << "发送视频帧失败!"<<  err;
            }

            // 5.2接收解码后的帧
            while(avcodec_receive_frame(pVideoCodecContext, pFrame) == 0) {
                if (!isPlay) {
                    break;
                }

#if defined(Q_OS_WIN)
                const enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_CUDA;
#elif defined(Q_OS_MAC)
                const enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
                AVFrame *sw_frame = nullptr;
                // 如果收到的是硬件帧，则先传输到系统内存
                if (pFrame->format == hw_pix_fmt) {
                    sw_frame = av_frame_alloc();
                    if (!sw_frame) {
                        qDebug() << "Failed to allocate sw_frame";
                        continue;
                    }
                    if (av_hwframe_transfer_data(sw_frame, pFrame, 0) < 0) {
                        qDebug() << "Error transferring frame from GPU to CPU";
                        av_frame_free(&sw_frame);
                        continue;   // 跳过这帧，避免使用错误数据
                    }
                } else {
                    sw_frame = av_frame_clone(pFrame);
                    if (!sw_frame) {
                        qDebug() << "Failed to clone frame";
                        continue;
                    }
                }

                // 计算有效播放时间（秒）
                double effectiveTime = (timer.elapsed() - startTime - pauseTotal) / 1000.0;
                double currentPts = pFrame->pts * av_q2d(avFormatContext->streams[videoStream]->time_base);
                currentPlaybackTime = currentPts;  // 更新当前播放时间
                emit sigUpdateTime(currentPts, totalDuration);  // 发射当前时间和总时长
                double delay = currentPts - effectiveTime;
                if (delay > 0) {
                    // 延时等待视频帧显示时机到来
                    QThread::msleep(static_cast<unsigned long>(delay * 1000));
                }

                emit sigGetFrame(sw_frame);
                av_frame_unref(pFrame);
            }
        }
        // 处理音频帧
        else if (packet->stream_index == audioStream) {
            err = avcodec_send_packet(pAudioCodecContext, packet);
            if (err != 0) {
                if (AVERROR(EAGAIN) == err) {
                    av_packet_unref(packet);
                    continue;
                }
                qDebug() << "发送音频帧失败!" << err;
            }

            while (avcodec_receive_frame(pAudioCodecContext, pFrame) == 0) {
                // 进行音频重采样并写入音频设备
                AVFrame *resampledFrame = av_frame_alloc();
                if (!resampledFrame) {
                    qDebug() << "Failed to allocate resampledFrame";
                    continue;
                }

                // 设定通道布局
                av_channel_layout_default(&resampledFrame->ch_layout, 2);  // 2 代表立体声
                resampledFrame->sample_rate = pAudioCodecContext->sample_rate;
                resampledFrame->format = AV_SAMPLE_FMT_S16;
                if (swr_convert_frame(swrCtx, resampledFrame, pFrame) < 0) {
                    qDebug() << "音频重采样失败";
                    av_frame_free(&resampledFrame);
                    continue;
                }

                // 计算音频数据大小
                int dataSize = av_samples_get_buffer_size(
                    nullptr,
                    2,
                    resampledFrame->nb_samples,
                    AV_SAMPLE_FMT_S16,
                    1);

                // 将数据写入 QAudioSink
                if (audioDevice) {
                    audioDevice->write((const char *)resampledFrame->data[0], dataSize);
                }

                av_frame_free(&resampledFrame);
            }
        }
        // 处理字幕包
        else if (packet->stream_index == subtitleStream && pSubtitleCodecContext) {
            AVSubtitle subtitle;
            memset(&subtitle, 0, sizeof(subtitle));
            int gotSubtitle = 0;
            err = avcodec_decode_subtitle2(pSubtitleCodecContext, &subtitle, &gotSubtitle, packet);
            if (err < 0) {
                qDebug() << "Decoding subtitle failed:" << err;
            } else if (gotSubtitle) {
                // 遍历每个 rect
                for (int i = 0; i < subtitle.num_rects; i++) {
                    AVSubtitleRect *rect = subtitle.rects[i];
                    // 如果是图像字幕（例如 PGS）
                    if (rect->type == SUBTITLE_BITMAP) {
                        int w = rect->w, h = rect->h;
                        if (w <= 0 || h <= 0 || !rect->data[0] || !rect->data[1]) {
                            continue;
                        }
                        // 创建一个 QImage 格式为 ARGB32
                        QImage img(w, h, QImage::Format_ARGB32);
                        // 对于每个像素，根据 rect->linesize[0] 获取索引值，然后查找对应颜色
                        for (int y = 0; y < h; y++) {
                            for (int x = 0; x < w; x++) {
                                // 这里假设 rect->linesize[0] 存在且有效
                                int index = rect->data[0][y * rect->linesize[0] + x];
                                // 注意：rect->colors 中的颜色通常存储为 ARGB 值
                                uint32_t color = rect->data[1][index];
                                img.setPixel(x, y, color);
                            }
                        }
                        // 转换为无 alpha 的格式测试
                        QImage showImg = img.convertToFormat(QImage::Format_RGB32);
                        // 发射图像字幕信号
                        emit sigSubtitleImage(img);
                        qDebug() << "发射图像字幕信号";
                    }
                    // 如果是 ASS 字幕（或普通文本字幕）
                    else if (rect->type == SUBTITLE_ASS) {
                        if (rect->ass) {
                            QString assText = QString::fromUtf8(rect->ass);
                            emit sigSubtitle(assText);
                        } else if (rect->text) {
                            QString text = QString::fromUtf8(rect->text);
                            emit sigSubtitle(text);
                        }
                    }
                    else if (rect->type == SUBTITLE_TEXT) {
                        if (rect->text) {
                            QString text = QString::fromUtf8(rect->text);
                            emit sigSubtitle(text);
                        }
                    }
                    else {
                        qDebug() << "Unknown subtitle rect type:" << rect->type;
                    }
                }
                avsubtitle_free(&subtitle);
            }
        }

        av_packet_unref(packet);

#ifdef Q_OS_WIN
        QCoreApplication::processEvents(); // 处理 queued 信号
#else
        // 每 5 次循环处理一次 queued 事件，减少对阻塞调用的影响
        static int loopCount = 0;
        loopCount++;
        if (loopCount % 5 == 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
            QThread::yieldCurrentThread();
        }
#endif
    }
    cleanup();
}
