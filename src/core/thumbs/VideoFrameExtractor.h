#pragma once

#include <QImage>
#include <QTransform>
#include <QString>

namespace kg {

/**
 * Extracts a poster frame from a video file using libavformat/libavcodec
 * directly, without Qt Multimedia and without an OpenGL context.
 *
 * Deliberately *not* built on mpv's render API: frame extraction has to work
 * headless (during `--no-ui` indexing and inside the ThumbnailService worker
 * pool), where there is no GL context to render into. Decoding a frame and
 * scaling it is pure CPU work.
 *
 * Every entry point is static and stateless, and therefore safe to call from
 * any thread. libavcodec is initialised internally by the API used here, so no
 * explicit global init is required.
 *
 * When the build has no FFmpeg, `KG_HAS_VIDEO_THUMBS` is undefined and every
 * call returns an invalid QImage / invalid Info, which the caller already
 * treats as "no thumbnail available".
 */
class VideoFrameExtractor
{
public:
    struct Info
    {
        bool valid = false;
        int width = 0;
        int height = 0;
        qint64 durationMs = 0;
        /// Rotation from the container's display matrix, clockwise, in {0,90,180,270}.
        /// Phone videos routinely carry 90/270 here; ignoring it produces sideways
        /// thumbnails even though the frame decoded perfectly.
        int rotationDegrees = 0;
    };

    /// Opens the container just far enough to report geometry and duration.
    static Info probe(const QString &path);

    /**
     * Decodes a single frame near `positionMs` and scales it to fit inside
     * `edge` (preserving aspect ratio, never upscaling). Returns a null QImage
     * on any failure. The container's rotation is applied before returning.
     */
    static QImage extractFrame(const QString &path, qint64 positionMs, int edge);

    /**
     * Picks a "poster" position: ~10% into the video, clamped to 1..10 s.
     * Opening frames are frequently black or a fade-in, so 0 ms is a poor choice.
     */
    static QImage extractPoster(const QString &path, int edge);

    /// True when FFmpeg support was compiled in.
    static bool available();
};

} // namespace kg
