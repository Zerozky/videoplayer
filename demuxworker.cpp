#include "demuxworker.h"
#include <QDebug>
#include <iostream>
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

DemuxWorker::DemuxWorker(QObject *parent):QObject (parent)
{
    qDebug()<<"Thread构造函数ID:"<<QThread::currentThreadId();
    filePath = "";
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

void mSleep(int msec)
{
    QTime n=QTime::currentTime();
    QTime now;
    do
    {
        now=QTime::currentTime();
    }while (n.msecsTo(now)<=msec);
}

void DemuxWorker::slotDoWork()
{
    if (filePath.isEmpty()) {
        qDebug() << "No file selected";
        return;
    }

    AVFormatContext *avFormatContext = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame	*pFrame = av_frame_alloc();
    int audioStream = -1;
    int videoStream = -1;
    int err = -1;

    QByteArray filePathBytes = filePath.toLocal8Bit();
    char *fileName = filePathBytes.data();
    // char *fileName = "D:/S02E001.mp4";
    // char *fileName = "http://demo-videos.qnsdk.com/bbk-H265-50fps.mp4";
    // char *fileName = "S:/电影下载/BMcwnuIW_TimeCut_Video_2022-05-24_16_38_25.mp4";
    // char *fileName = "S:/电影下载/Resident.Evil.2002.2160p.BluRay.REMUX.HEVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT/Resident.Evil.2002.2160p.BluRay.REMUX.HEVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT.mkv";

    isPlay = true;
    isPause = false;

    //1.分配解复用器上下文
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate context.\n");
        return;
    }

    //2.根据URL打开本地文件或网络流
    if(avformat_open_input(&avFormatContext, fileName, nullptr, nullptr) != 0){
        qDebug() << "Couldn't open file";
        return;
    }

    //3.读取媒体的部分数据包以获取码流信息
    // Retrieve stream information
    if(avformat_find_stream_info(avFormatContext, nullptr)<0){
        qDebug() << "Couldn't find stream information";
        return;
    }

    //查找音视频流索引
    for(unsigned int i = 0; i < avFormatContext->nb_streams; i++){
        if(avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audioStream = static_cast<int>(i);
        }
        else if(avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            videoStream = static_cast<int>(i);
        }
    }
    if(audioStream == -1 && videoStream == -1){
        qDebug() << "Didn't find a valid stream";
        return;
    }

    //视频
    AVCodecContext *pVideoCodecContext = nullptr;
    const AVCodec   *pCodecVideo = nullptr;
    AVCodecParameters *pVideoCodecPara = nullptr;

    if(videoStream != -1)
    {
        pVideoCodecPara = avFormatContext->streams[videoStream]->codecpar;

        //1.分配编解码器上下文
        pVideoCodecContext =  avcodec_alloc_context3(nullptr);
        if (!pVideoCodecContext){
            qDebug() <<  "avcodec_alloc_context3";
            return;
        }

        //2.将码流中的编解码器信息拷贝到AVCodecContext
        err = avcodec_parameters_to_context(pVideoCodecContext, pVideoCodecPara);
        if (err < 0){
            qDebug() << "avcodec_parameters_to_context";
            return;
        }

        //3.根据编解码器信息查找相应的解码器
        pCodecVideo = avcodec_find_decoder(pVideoCodecPara->codec_id);
        if(pCodecVideo == nullptr){
            qDebug() <<  "avcodec_find_decoder";
            return;
        }

        qDebug() << "编解码器名:" << pCodecVideo->long_name;

        //4.打开编解码器并关联到AVCodecContext
        err = avcodec_open2(pVideoCodecContext, pCodecVideo, nullptr);
        if(err){
            qDebug() << "avcodec_open2";
            return;
        }

        emit sigGetVideoInfo(pVideoCodecContext->width, pVideoCodecContext->height);
        qDebug() << "视频宽度:" << pVideoCodecContext->width << "高度:" << pVideoCodecContext->height;
    }

    // === 音频解码部分 ===
    AVCodecContext *pAudioCodecContext = nullptr;
    QAudioSink *audioSink = nullptr;
    QIODevice *audioDevice = nullptr;
    SwrContext *swrCtx = nullptr;

    if (audioStream != -1) {
        AVCodecParameters *pAudioCodecPara = avFormatContext->streams[audioStream]->codecpar;
        pAudioCodecContext = avcodec_alloc_context3(nullptr);
        avcodec_parameters_to_context(pAudioCodecContext, pAudioCodecPara);
        const AVCodec *pCodecAudio = avcodec_find_decoder(pAudioCodecPara->codec_id);
        avcodec_open2(pAudioCodecContext, pCodecAudio, nullptr);

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
        // 获取 QIODevice 用于写入音频数据
        audioDevice = audioSink->start();

        // 初始化 SwrContext（音频重采样）
        swrCtx = swr_alloc();
        // 使用 AVChannelLayout 代替 channel_layout
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, pAudioCodecPara->ch_layout.nb_channels);

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
        }
    }

    QElapsedTimer timer;
    double lastFrameTime = 0;
    timer.start(); // 计时器开始

    for (;;) {
        if (isPause)
        {
            mSleep(1000);
            continue;
        }

        //4.从文件中获取数据包
        if(av_read_frame(avFormatContext, packet) < 0){
            goto end;
        }

        // 处理视频帧
        if(packet->stream_index == videoStream)
        {
            //5.1向解码器发送数据包
            err = avcodec_send_packet(pVideoCodecContext, packet);
            if(err != 0) {
                if(AVERROR(EAGAIN) == err)
                    continue;
                qDebug() << "发送视频帧失败!"<<  err;
            }

            //5.2接收解码后的帧
            while(avcodec_receive_frame(pVideoCodecContext, pFrame) == 0) {
                // 计算当前帧的播放时间 (秒)
                double currentPts = pFrame->pts * av_q2d(avFormatContext->streams[videoStream]->time_base);

                // 计算播放同步时间
                // double elapsedTime = timer.elapsed() / 1000.0;  // 当前系统时间，毫秒转换为秒

                // 采用音频时钟作为同步基准，如果没有音频则回退到 QElapsedTimer（你可以提前启动一个 timer）
                double audioTime = audioSink ? (audioSink->elapsedUSecs() / 1000000.0) : (timer.elapsed() / 1000.0);

                // 计算帧与上一帧的延迟
                double delay = currentPts - audioTime;

                if (delay > 0) {
                    // 延时等待视频帧显示时机到来
                    QThread::msleep(static_cast<unsigned long>(delay * 1000));
                }

                // 克隆当前帧，确保传递给 UI 的数据不会被后续解码覆盖
                AVFrame *frameClone = av_frame_clone(pFrame);
                if (!frameClone) {
                    qDebug() << "克隆视频帧失败";
                } else {
                    emit sigGetFrame(frameClone);
                }
            }
        }
        // 处理音频帧
        else if (packet->stream_index == audioStream) {
            err = avcodec_send_packet(pAudioCodecContext, packet);
            if (err != 0) {
                if (AVERROR(EAGAIN) == err)
                    continue;
                qDebug() << "发送音频帧失败!" << err;
            }

            while (avcodec_receive_frame(pAudioCodecContext, pFrame) == 0) {
                // 进行音频重采样
                AVFrame *resampledFrame = av_frame_alloc();
                // 使用 av_channel_layout_default 设定通道布局
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
                    nullptr, 2, resampledFrame->nb_samples,
                    AV_SAMPLE_FMT_S16, 1);

                // 将数据写入 QAudioSink
                if (audioDevice) {
                    audioDevice->write((const char *)resampledFrame->data[0], dataSize);
                }

                av_frame_free(&resampledFrame);
            }
        }

        av_packet_unref(packet); // 注意清理，容易造成内存泄漏
        continue;

        if (!isPlay)
        {
            goto end;
        }
    }

end:
    //6.关闭解码器和释放上下文
    avcodec_close(pVideoCodecContext);
    avcodec_close(pAudioCodecContext);
    //5.关闭解复用器
    avformat_close_input(&avFormatContext);
    qDebug() << "end of play";
}
