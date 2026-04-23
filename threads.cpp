#include "threads.h"
#include "video.h"
#include "magic.h"
#include <QSet>
#include <QFileInfo>
#include <QImageReader>
#include <QDirIterator>
#include <QDebug>
#ifdef PDF_ENABLED
#include <poppler-qt5.h>
#endif

ImageLoaderThread::ImageLoaderThread()
    : mFirst(0), mLast(0), mAborted(false), mPending(0)
{
}

ImageLoaderThread::~ImageLoaderThread()
{
    clear();
}

void ImageLoaderThread::load(QImageReader *reader, uint flags, int rotation, void *userData, const QSize &size, const QString &path)
{
    Node *node = new Node;
    node->next = 0;
    node->flags = flags;
    node->rotation = rotation % 360;
    node->size = size;
    node->reader = reader;
    node->path = path.isEmpty() && reader ? reader->fileName() : path;
    node->userData = userData;
    if (rotation % 180 == 90)
        qSwap(node->size.rwidth(), node->size.rheight());
    QMutexLocker lock(&mMutex);
    ++mPending;
    Q_ASSERT(!mFirst == !mLast);
    if (!mFirst) {
        mFirst = mLast = node;
    } else {
        Q_ASSERT(mLast && mFirst);
        if (flags & HighPriority) {
            node->next = mFirst;
            mFirst = node;
        } else {
            Q_ASSERT(mLast);
            mLast->next = node;
            mLast = node;
        }
    }
    mWaitCondition.wakeOne();
}

bool ImageLoaderThread::remove(void *userData)
{
    QMutexLocker lock(&mMutex);
    Node *prev = 0;
    Node *n = mFirst;
    while (n && n->userData != userData) {
        prev = n;
        n = n->next;
    }
    if (n) {
        if (n == mFirst && n == mLast) {
            mFirst = mLast = 0;
        } else if (n == mLast) {
            mLast = prev;
            mLast->next = 0;
        } else if (n == mFirst) {
            mFirst = n->next;
            Q_ASSERT(!prev);
            Q_ASSERT(mFirst);
        } else {
            prev->next = n->next;
        }
        --mPending;
        delete n;
    }
    mWaitCondition.wakeOne();
    return n;
}


void ImageLoaderThread::clear()
{
    QMutexLocker lock(&mMutex);
    while (mFirst) {
        Node *n = mFirst;
        mFirst = mFirst->next;
        delete n;
    }
    mPending = 0;
    mFirst = mLast = 0;
}


void ImageLoaderThread::run()
{
    while (!mAborted) {
        Node *node = 0;
        {
            QMutexLocker lock(&mMutex);
            while (!node) {
                if (mFirst) {
                    node = mFirst;
                    mFirst = mFirst->next;
                    if (!mFirst)
                        mLast = 0;
                } else {
                    mWaitCondition.wait(&mMutex);
                    if (mAborted)
                        return;
                }
            }
            --mPending;
        }
        QImage img;
        QSize originalSize;
        const bool isPdf = node->path.endsWith(".pdf", Qt::CaseInsensitive);
#ifdef PDF_ENABLED
        if (isPdf) {
            // Poppler renders PDF pages to a QImage at a chosen DPI. We pick a
            // DPI that scales page 0's native point size (1pt = 1/72") to fit
            // the requested node->size while preserving aspect ratio.
            QScopedPointer<Poppler::Document> doc(Poppler::Document::load(node->path));
            if (doc && !doc->isLocked() && doc->numPages() > 0) {
                doc->setRenderHint(Poppler::Document::Antialiasing, true);
                doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
                QScopedPointer<Poppler::Page> page(doc->page(0));
                if (page) {
                    const QSizeF pt = page->pageSizeF();
                    originalSize = QSize(qRound(pt.width()), qRound(pt.height()));
                    double dpi = 150.0;
                    if (!node->size.isEmpty() && pt.width() > 0 && pt.height() > 0) {
                        const double sx = (node->size.width() * 72.0) / pt.width();
                        const double sy = (node->size.height() * 72.0) / pt.height();
                        dpi = qMin(sx, sy);
                    }
                    img = page->renderToImage(dpi, dpi);
                }
            }
        } else
#else
        if (isPdf) {
            // PDF support not compiled in; fall through to image-reader path
            // which will simply fail to load the file.
        } else
#endif
        {
            if (node->reader) {
                node->reader->setAutoTransform(true);
                originalSize = node->reader->size();
                // QImageReader::size() returns the size BEFORE EXIF auto-transform.
                // If the EXIF transform swaps width/height (90/270 rotation or the
                // corresponding mirrored variants), pre-swap so the aspect-fit
                // calculation against node->size operates on the post-transform
                // orientation; otherwise setScaledSize yields an image that exceeds
                // the requested bounds after Qt applies the rotation.
                const QImageIOHandler::Transformations tr = node->reader->transformation();
                if (tr & (QImageIOHandler::TransformationRotate90)) {
                    originalSize.transpose();
                }
                QSize size;
                if (!node->size.isEmpty()) {
                    size = originalSize;
                    size.scale(node->size, Qt::KeepAspectRatio);
                    if (!(node->flags & NoSmoothScale)) {
                        QSize readerScaled = size;
                        if (tr & (QImageIOHandler::TransformationRotate90)) {
                            readerScaled.transpose();
                        }
                        node->reader->setScaledSize(readerScaled);
                    }
                }
                if (node->reader->read(&img) && (node->flags & NoSmoothScale) && !size.isNull()) {
                    img = img.scaled(size);
                }
            }
        }
        if (img.isNull()) {
            emit loadError(node->userData);
        } else {
            emit imageLoaded(node->userData, img, originalSize);
        }
        delete node;
    }
}

void ImageLoaderThread::abort()
{
    QMutexLocker locker(&mMutex);
    mAborted = true;
    mWaitCondition.wakeOne();
}

ThumbLoaderThread::ThumbLoaderThread(const QImage &image, int w)
    : QThread(), original(image), width(w)
{
    Q_ASSERT(!image.isNull());
}

void ThumbLoaderThread::run()
{
    const QImage thumb = original.scaledToWidth(width);
    emit thumbLoaded(thumb);
}

FileNameThread::FileNameThread(const QString &dir, /*int min, int max, */const QRegExp &rx, const QRegExp &irx, bool detect, bool rec)
    : QThread(), directory(dir), /*minDepth(min), maxDepth(max), */aborted(false),
      regexp(rx), ignore(irx), detectFileName(detect), recurse(rec),
      minSize(-1), maxSize(-1)
{
}


static inline int matchSize(int min, int max, const QFileInfo &fi)
{
    if ((min != -1 && fi.size() < min * 1024) || (max != -1 && fi.size() > max * 1024)) {
        return false;
    } else {
        return true;
    }
}

bool FileNameThread::matches(const QString &absoluteFilePath) const
{
    return (regexp.isEmpty() || absoluteFilePath.contains(regexp))
        && (ignore.isEmpty() || !absoluteFilePath.contains(ignore));
}

void FileNameThread::run()
{
#ifdef MAGIC_ENABLED
    MagicCookie cookie;
#else
    QSet<QString> formats;
    if (!detectFileName) {
        const QList<QByteArray> ba = QImageReader::supportedImageFormats();
        for (int i=0; i<ba.size(); ++i) {
            QString string = QString::fromLocal8Bit(ba.at(i));
            formats.insert(string);
            formats.insert(string.toUpper());
        }
#ifdef PDF_ENABLED
        formats.insert("pdf");
        formats.insert("PDF");
#endif
#ifdef VIDEO_ENABLED
        static const char *const videoExts[] = {
            "mp4", "mov", "mkv", "webm", "avi",
            "m4v", "mpg", "mpeg", "wmv", "flv",
            "3gp", "ogv", "ts", 0
        };
        for (int i = 0; videoExts[i]; ++i) {
            formats.insert(QString::fromLatin1(videoExts[i]));
            formats.insert(QString::fromLatin1(videoExts[i]).toUpper());
        }
#endif
    }
#endif

    QDirIterator it(directory, QDir::NoDotAndDotDot|QDir::Files|QDir::Dirs,
                    recurse ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    int index = 0;
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (::matchSize(minSize, maxSize, fi)) {
            const QString absoluteFilePath = fi.absoluteFilePath();
#ifdef MAGIC_ENABLED
            if (matches(absoluteFilePath) && cookie.isSupported(absoluteFilePath)) {
                emit file(absoluteFilePath);
            }
#else
            if (detectFileName) {
                if (matches(absoluteFilePath) && ImageLoaderThread::canLoad(absoluteFilePath)) {
                    emit file(absoluteFilePath);
                }
            } else if (formats.contains(fi.suffix()) && matches(absoluteFilePath)) {
                emit file(absoluteFilePath);
            }
#endif
        }
        if (++index % 10 == 0 && isAborted()) {
            break;
        }
    }
}

bool FileNameThread::isAborted() const
{
    QMutexLocker locker(&abortMutex);
    return aborted;
}
void FileNameThread::abort()
{
    QMutexLocker locker(&abortMutex);
    aborted = true;
}

void FileNameThread::setSizeConstraints(int min, int max)
{
    minSize = min;
    maxSize = max;
}

bool ImageLoaderThread::canLoad(const QString &fileName)
{
#ifdef MAGIC_ENABLED
    MagicCookie cookie;
    return cookie.isSupported(fileName);
#else
    if (!QImageReader::imageFormat(fileName).isEmpty()) {
        return true;
    }
#ifdef PDF_ENABLED
    if (fileName.endsWith(".pdf", Qt::CaseInsensitive)) {
        return true;
    }
#endif
    if (isVideoPath(fileName)) {
        return true;
    }
    return false;
#endif
}

int ImageLoaderThread::pending() const
{
    QMutexLocker lock(&mMutex);
    return mPending;
}
