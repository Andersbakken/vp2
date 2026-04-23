#include "video.h"

#ifdef VIDEO_ENABLED

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QFileInfo>
#include <QSet>

class VideoDecoderPrivate
{
public:
    VideoDecoderPrivate()
        : fmtCtx(0), codecCtx(0), swsCtx(0), frame(0), rgbFrame(0), packet(0),
          streamIndex(-1), durationSec(0.0), fps(0.0), currentSec(0.0)
    {
        timeBase.num = 0;
        timeBase.den = 1;
    }

    AVFormatContext *fmtCtx;
    AVCodecContext *codecCtx;
    SwsContext *swsCtx;
    AVFrame *frame;
    AVFrame *rgbFrame;
    AVPacket *packet;
    int streamIndex;
    double durationSec;
    double fps;
    double currentSec;
    AVRational timeBase;
    QSize size;
    QSize targetSize;
    QSize lastScaleTarget;
};

VideoDecoder::VideoDecoder() : d(new VideoDecoderPrivate) {}

VideoDecoder::~VideoDecoder()
{
    close();
    delete d;
}

bool VideoDecoder::isOpen() const
{
    return d->fmtCtx != 0;
}

void VideoDecoder::setTargetSize(const QSize &size)
{
    d->targetSize = size;
}

void VideoDecoder::close()
{
    if (d->swsCtx) {
        sws_freeContext(d->swsCtx);
        d->swsCtx = 0;
    }
    if (d->rgbFrame) {
        av_frame_free(&d->rgbFrame);
    }
    if (d->frame) {
        av_frame_free(&d->frame);
    }
    if (d->packet) {
        av_packet_free(&d->packet);
    }
    if (d->codecCtx) {
        avcodec_free_context(&d->codecCtx);
    }
    if (d->fmtCtx) {
        avformat_close_input(&d->fmtCtx);
    }
    d->streamIndex = -1;
    d->durationSec = 0.0;
    d->fps = 0.0;
    d->currentSec = 0.0;
    d->timeBase.num = 0;
    d->timeBase.den = 1;
    d->size = QSize();
}

bool VideoDecoder::open(const QString &path)
{
    close();
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&d->fmtCtx, utf8.constData(), 0, 0) != 0) {
        return false;
    }
    if (avformat_find_stream_info(d->fmtCtx, 0) < 0) {
        close();
        return false;
    }
    // Find the first video stream.
    for (unsigned i = 0; i < d->fmtCtx->nb_streams; ++i) {
        if (d->fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            d->streamIndex = int(i);
            break;
        }
    }
    if (d->streamIndex < 0) {
        close();
        return false;
    }
    AVStream *st = d->fmtCtx->streams[d->streamIndex];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        close();
        return false;
    }
    d->codecCtx = avcodec_alloc_context3(codec);
    if (!d->codecCtx) {
        close();
        return false;
    }
    if (avcodec_parameters_to_context(d->codecCtx, st->codecpar) < 0) {
        close();
        return false;
    }
    if (avcodec_open2(d->codecCtx, codec, 0) < 0) {
        close();
        return false;
    }

    d->size = QSize(d->codecCtx->width, d->codecCtx->height);
    d->timeBase = st->time_base;
    if (d->fmtCtx->duration > 0) {
        d->durationSec = double(d->fmtCtx->duration) / double(AV_TIME_BASE);
    }
    if (st->avg_frame_rate.den > 0) {
        d->fps = double(st->avg_frame_rate.num) / double(st->avg_frame_rate.den);
    }
    if (d->fps <= 0.0) {
        d->fps = 25.0;
    }
    d->currentSec = 0.0;

    d->frame = av_frame_alloc();
    d->rgbFrame = av_frame_alloc();
    d->packet = av_packet_alloc();
    if (!d->frame || !d->rgbFrame || !d->packet) {
        close();
        return false;
    }
    return true;
}

QSize VideoDecoder::frameSize() const
{
    return d->size;
}

double VideoDecoder::durationSeconds() const
{
    return d->durationSec;
}

double VideoDecoder::currentSeconds() const
{
    return d->currentSec;
}

double VideoDecoder::frameRate() const
{
    return d->fps;
}

bool VideoDecoder::decodeNextFrame(QImage *out)
{
    if (!d->fmtCtx || !d->codecCtx) {
        return false;
    }
    // Pump packets until we pull one full decoded frame out of the codec.
    while (true) {
        int ret = avcodec_receive_frame(d->codecCtx, d->frame);
        if (ret == 0) {
            break;
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            return false;
        }
        // Need more input: read packets until we find one for our stream, then
        // send it. av_read_frame returns EOF when the file is exhausted.
        int readRet = 0;
        do {
            av_packet_unref(d->packet);
            readRet = av_read_frame(d->fmtCtx, d->packet);
            if (readRet < 0) {
                break;
            }
        } while (d->packet->stream_index != d->streamIndex);

        if (readRet < 0) {
            // Flush the decoder. A subsequent receive_frame can still produce
            // a final frame; if it doesn't, we return false to signal EOF.
            avcodec_send_packet(d->codecCtx, 0);
            int flushRet = avcodec_receive_frame(d->codecCtx, d->frame);
            if (flushRet == 0) {
                break;
            }
            return false;
        }
        if (avcodec_send_packet(d->codecCtx, d->packet) < 0) {
            av_packet_unref(d->packet);
            return false;
        }
        av_packet_unref(d->packet);
    }

    const int w = d->frame->width;
    const int h = d->frame->height;
    if (w <= 0 || h <= 0) {
        return false;
    }

    // Convert frame pts (in stream timebase) to seconds. best_effort_timestamp
    // handles containers where pts is AV_NOPTS_VALUE but dts is usable.
    const int64_t pts = d->frame->best_effort_timestamp;
    if (pts != AV_NOPTS_VALUE && d->timeBase.den > 0) {
        d->currentSec = double(pts) * double(d->timeBase.num) / double(d->timeBase.den);
    }

    int outW = w;
    int outH = h;
    if (!d->targetSize.isEmpty()) {
        QSize fit(w, h);
        fit.scale(d->targetSize, Qt::KeepAspectRatio);
        outW = qMax(1, fit.width());
        outH = qMax(1, fit.height());
    }
    d->swsCtx = sws_getCachedContext(
        d->swsCtx,
        w, h, AVPixelFormat(d->frame->format),
        outW, outH, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, 0, 0, 0);
    if (!d->swsCtx) {
        return false;
    }

    *out = QImage(outW, outH, QImage::Format_RGBA8888);
    uint8_t *dstData[4] = { out->bits(), 0, 0, 0 };
    int dstStride[4] = { out->bytesPerLine(), 0, 0, 0 };
    sws_scale(d->swsCtx, d->frame->data, d->frame->linesize, 0, h,
              dstData, dstStride);
    return true;
}

bool VideoDecoder::seek(double seconds)
{
    if (!d->fmtCtx) {
        return false;
    }
    const int64_t ts = int64_t(seconds * AV_TIME_BASE);
    if (av_seek_frame(d->fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(d->codecCtx);
    d->currentSec = seconds;
    return true;
}

bool isVideoPath(const QString &path)
{
    // Common container extensions. libav can decode many more, but gating on
    // extension avoids probing every file during directory scan.
    static const QSet<QString> exts = QSet<QString>()
        << "mp4" << "mov" << "mkv" << "webm" << "avi"
        << "m4v" << "mpg" << "mpeg" << "wmv" << "flv"
        << "3gp" << "ogv" << "ts";
    return exts.contains(QFileInfo(path).suffix().toLower());
}

#endif
