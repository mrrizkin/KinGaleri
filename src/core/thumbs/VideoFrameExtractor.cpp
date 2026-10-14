#include "core/thumbs/VideoFrameExtractor.h"

#ifdef KG_HAS_VIDEO_THUMBS

#include <QtGlobal>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/cpu.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace kg {
namespace {

/// Owns the three libavformat/libavcodec objects involved in reading one frame
/// and releases them in the right order on every exit path, including early
/// returns. libav C handles are not RAII, and forgetting one here leaks an
/// fd / hundreds of MB per failed thumbnail.
struct AvContext
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *sws = nullptr;
    int streamIndex = -1;

    ~AvContext()
    {
        if (sws) {
            sws_freeContext(sws);
        }
        if (packet) {
            av_packet_free(&packet);
        }
        if (frame) {
            av_frame_free(&frame);
        }
        if (dec) {
            avcodec_free_context(&dec);
        }
        if (fmt) {
            avformat_close_input(&fmt);
        }
    }

    AvContext(const AvContext &) = delete;
    AvContext &operator=(const AvContext &) = delete;
    AvContext() = default;
};

/// Snaps the container display matrix to one of 0/90/180/270 degrees clockwise.
/// Returns 0 when absent, malformed, or not a rotation matrix.
int rotationFromStream(const AVStream *stream)
{
    if (!stream || !stream->codecpar) {
        return 0;
    }

    const AVPacketSideData *sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                         stream->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * static_cast<int>(sizeof(int32_t))) {
        return 0;
    }

    // av_display_rotation_get() returns degrees counter-clockwise, and NaN when
    // the matrix is not a pure rotation.
    const double ccw = av_display_rotation_get(reinterpret_cast<const int32_t *>(sd->data));
    if (std::isnan(ccw) || !std::isfinite(ccw)) {
        return 0;
    }

    // Negate to get the clockwise angle that the *decoder output* must be rotated
    // by in order to be displayed upright, then snap.
    double cw = -ccw;
    cw = std::fmod(cw, 360.0);
    if (cw < 0.0) {
        cw += 360.0;
    }

    const int snapped = static_cast<int>(std::lround(cw / 90.0)) * 90;
    return ((snapped % 360) + 360) % 360;
}

QImage applyRotation(const QImage &image, int degrees)
{
    switch (((degrees % 360) + 360) % 360) {
    case 90:
        return image.transformed(QTransform().rotate(90), Qt::SmoothTransformation);
    case 180:
        return image.transformed(QTransform().rotate(180), Qt::SmoothTransformation);
    case 270:
        return image.transformed(QTransform().rotate(270), Qt::SmoothTransformation);
    default:
        return image;
    }
}

/// Fills `out` but stops before opening any decoder. Used by probe().
bool openContainer(const QString &path, AvContext &out)
{
    if (path.isEmpty()) {
        return false;
    }

    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&out.fmt, utf8.constData(), nullptr, nullptr) < 0) {
        return false;
    }
    if (avformat_find_stream_info(out.fmt, nullptr) < 0) {
        return false;
    }

    const int index = av_find_best_stream(out.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        return false;
    }
    out.streamIndex = index;
    return true;
}

} // namespace

bool VideoFrameExtractor::available()
{
    return true;
}

VideoFrameExtractor::Info VideoFrameExtractor::probe(const QString &path)
{
    Info info;
    AvContext ctx;
    if (!openContainer(path, ctx)) {
        return info;
    }

    const AVStream *stream = ctx.fmt->streams[ctx.streamIndex];
    info.valid = true;
    info.width = stream->codecpar->width;
    info.height = stream->codecpar->height;
    info.rotationDegrees = rotationFromStream(stream);

    // Prefer the stream duration; fall back to the container's (AV_TIME_BASE units).
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        info.durationMs = av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1000});
    } else if (ctx.fmt->duration != AV_NOPTS_VALUE && ctx.fmt->duration > 0) {
        info.durationMs = ctx.fmt->duration / (AV_TIME_BASE / 1000);
    }

    // A rotated phone video reports 1920x1080 but displays as 1080x1920; report
    // what the viewer will actually see.
    if (info.rotationDegrees == 90 || info.rotationDegrees == 270) {
        std::swap(info.width, info.height);
    }
    return info;
}

QImage VideoFrameExtractor::extractFrame(const QString &path, qint64 positionMs, int edge)
{
    if (edge <= 0) {
        return QImage();
    }

    AvContext ctx;
    if (!openContainer(path, ctx)) {
        return QImage();
    }

    AVStream *stream = ctx.fmt->streams[ctx.streamIndex];

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        return QImage();
    }

    ctx.dec = avcodec_alloc_context3(codec);
    if (!ctx.dec) {
        return QImage();
    }
    if (avcodec_parameters_to_context(ctx.dec, stream->codecpar) < 0) {
        return QImage();
    }
    // Thumbnails only: never spin up more than a couple of decode threads per
    // job, because this runs inside the shared ThumbnailService pool and many
    // videos are queued at once. Over-subscribing here stalls the pool.
    ctx.dec->thread_count = std::min(2, std::max(1, av_cpu_count() / 2));
    if (avcodec_open2(ctx.dec, codec, nullptr) < 0) {
        return QImage();
    }

    ctx.frame = av_frame_alloc();
    ctx.packet = av_packet_alloc();
    if (!ctx.frame || !ctx.packet) {
        return QImage();
    }

    // Seek backwards so the target frame is decoded from the preceding keyframe.
    if (positionMs > 0) {
        const qint64 clampedUs = std::min<qint64>(positionMs, 24LL * 60 * 60 * 1000) * 1000;
        const int64_t ts = av_rescale_q(clampedUs, AVRational{1, 1000000}, stream->time_base);
        if (av_seek_frame(ctx.fmt, ctx.streamIndex, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(ctx.dec);
        }
        // A failed seek is not fatal: falling through decodes from the start.
    }

    QImage result;
    qint64 framesSeen = 0;
    bool done = false;

    while (!done && av_read_frame(ctx.fmt, ctx.packet) >= 0) {
        if (ctx.packet->stream_index != ctx.streamIndex) {
            av_packet_unref(ctx.packet);
            continue;
        }

        if (avcodec_send_packet(ctx.dec, ctx.packet) < 0) {
            av_packet_unref(ctx.packet);
            continue;
        }
        av_packet_unref(ctx.packet);

        while (avcodec_receive_frame(ctx.dec, ctx.frame) == 0) {
            ++framesSeen;
            // Skip a couple of frames after a seek: the first decoded frame is
            // frequently a partially-rendered leftover from the keyframe.
            if (framesSeen < 2) {
                av_frame_unref(ctx.frame);
                continue;
            }

            const int srcW = ctx.frame->width;
            const int srcH = ctx.frame->height;
            if (srcW <= 0 || srcH <= 0) {
                av_frame_unref(ctx.frame);
                continue;
            }

            const int rotation = rotationFromStream(stream);

            // Scale against the *coded* frame geometry, then rotate. Rotating and
            // pre-swapping the target size would apply the rotation twice and emit a
            // landscape thumbnail for a portrait phone video.
            const double scale = std::min(1.0, std::min(static_cast<double>(edge) / srcW,
                                                           static_cast<double>(edge) / srcH));
            const int dstW = std::max(1, static_cast<int>(std::lround(srcW * scale)));
            const int dstH = std::max(1, static_cast<int>(std::lround(srcH * scale)));

            QImage scaled(dstW, dstH, QImage::Format_RGB32);
            if (scaled.isNull()) {
                av_frame_unref(ctx.frame);
                done = true;
                break;
            }

            // AV_PIX_FMT_BGRA lays out bytes B,G,R,A, which is exactly
            // QImage::Format_RGB32 on little-endian targets.
            uint8_t *dstData[4] = {scaled.bits(), nullptr, nullptr, nullptr};
            int dstLinesize[4] = {static_cast<int>(scaled.bytesPerLine()), 0, 0, 0};

            ctx.sws = sws_getCachedContext(ctx.sws, srcW, srcH, ctx.dec->pix_fmt,
                                          dstW, dstH, AV_PIX_FMT_BGRA,
                                          SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (ctx.sws) {
                sws_scale(ctx.sws, ctx.frame->data, ctx.frame->linesize, 0, srcH, dstData, dstLinesize);
                result = applyRotation(scaled, rotation);
            }

            av_frame_unref(ctx.frame);
            done = true;
            break;
        }
    }

    return result;
}

QImage VideoFrameExtractor::extractPoster(const QString &path, int edge)
{
    const Info info = probe(path);

    // ~10% in, clamped to 1..10 s. Frame 0 is very often black or a fade.
    qint64 positionMs = 1000;
    if (info.valid && info.durationMs > 0) {
        positionMs = std::clamp<qint64>(info.durationMs / 10, 1000, 10000);
    }
    return extractFrame(path, positionMs, edge);
}

} // namespace kg

#else // !KG_HAS_VIDEO_THUMBS

namespace kg {

bool VideoFrameExtractor::available()
{
    return false;
}

VideoFrameExtractor::Info VideoFrameExtractor::probe(const QString &)
{
    return Info{};
}

QImage VideoFrameExtractor::extractFrame(const QString &, qint64, int)
{
    return QImage();
}

QImage VideoFrameExtractor::extractPoster(const QString &, int)
{
    return QImage();
}

} // namespace kg

#endif
