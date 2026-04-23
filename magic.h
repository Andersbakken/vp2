#ifndef VP2_MAGIC_H
#define VP2_MAGIC_H

#include <QString>

#ifdef MAGIC_ENABLED

// Thin RAII wrapper around libmagic's magic_t cookie. Each instance owns a
// private cookie; libmagic is not thread-safe for a single cookie, so pass
// one MagicCookie per thread (or per loop body that touches it).
class MagicCookie
{
public:
    MagicCookie();
    ~MagicCookie();

    // Classify the file at `path`. Returns true if it's an image, video (when
    // VIDEO_ENABLED was built in) or PDF (when PDF_ENABLED was built in).
    // Returns false on read errors, unrecognized formats, or cookie init
    // failure.
    bool isSupported(const QString &path) const;

private:
    MagicCookie(const MagicCookie &);
    MagicCookie &operator=(const MagicCookie &);

    void *mCookie;
};

#endif

#endif
