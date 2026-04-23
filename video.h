#ifndef VIDEO_H
#define VIDEO_H

#include <QImage>
#include <QString>

#ifdef VIDEO_ENABLED

class VideoDecoderPrivate;

class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();

    bool open(const QString &path);
    void close();
    bool isOpen() const;

    QSize frameSize() const;
    double durationSeconds() const;
    double currentSeconds() const;
    double frameRate() const;

    // Target output size for decoded frames. Empty = native resolution. When
    // set to a non-empty size, decoded frames are scaled to fit within it
    // while preserving aspect ratio (via swscale).
    void setTargetSize(const QSize &size);

    bool decodeNextFrame(QImage *out);
    bool seek(double seconds);

private:
    VideoDecoderPrivate *d;
};

bool isVideoPath(const QString &path);

#else

inline bool isVideoPath(const QString &) { return false; }

#endif

#endif
