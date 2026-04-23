#include "magic.h"

#ifdef MAGIC_ENABLED

#include <magic.h>

MagicCookie::MagicCookie() : mCookie(0)
{
    magic_t cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_ERROR);
    if (!cookie) {
        return;
    }
    if (magic_load(cookie, 0) != 0) {
        magic_close(cookie);
        return;
    }
    mCookie = cookie;
}

MagicCookie::~MagicCookie()
{
    if (mCookie) {
        magic_close(static_cast<magic_t>(mCookie));
    }
}

bool MagicCookie::isSupported(const QString &path) const
{
    if (!mCookie) {
        return false;
    }
    const QByteArray utf8 = path.toUtf8();
    const char *mime = magic_file(static_cast<magic_t>(mCookie), utf8.constData());
    if (!mime) {
        return false;
    }
    // libmagic returns strings like "image/jpeg", "video/mp4", "application/pdf".
    // Cheap prefix checks avoid QString allocation per file.
    if (qstrncmp(mime, "image/", 6) == 0) {
        return true;
    }
#ifdef VIDEO_ENABLED
    if (qstrncmp(mime, "video/", 6) == 0) {
        return true;
    }
#endif
#ifdef PDF_ENABLED
    if (qstrcmp(mime, "application/pdf") == 0) {
        return true;
    }
#endif
    return false;
}

#endif
