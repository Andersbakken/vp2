#include "threads.h"
#include <QSet>
#include <QFileInfo>
#include <QImageReader>
#include <QDirIterator>
#include <QDebug>
#ifdef MAGICK_ENABLED
#include <Magick++/Image.h>
#include <Magick++/Geometry.h>
#endif

ImageLoaderThread::ImageLoaderThread()
    : mFirst(0), mLast(0), mAborted(false), mPending(0)
{
}

ImageLoaderThread::~ImageLoaderThread()
{
    clear();
}

void ImageLoaderThread::load(const QString &p, uint flags, int rotation, void *userData, const QSize &size)
{
    Node *node = new Node;
    node->next = 0;
    node->flags = flags;
    node->rotation = rotation % 360;
    node->size = size;
    node->path = p;
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
#ifdef MAGICK_ENABLED
        if (node->path.endsWith(".pdf", Qt::CaseInsensitive)) {
            try { 
                Magick::Image pdf(node->path.toStdString());
                if (pdf.isValid()) {
                    if (!node->size.isNull()) {
                        QSize size(pdf.columns(), pdf.rows());
                        size.scale(node->size, Qt::KeepAspectRatio);
                        pdf.scale(Magick::Geometry(size.width(), size.height()));
                    }
                    img = QImage(pdf.columns(), pdf.rows(), QImage::Format_RGB32);
                    pdf.write(0, 0, img.width(), img.height(), "RGB", Magick::CharPixel, img.bits());
                }
            } catch (...) {
            }
        } else
#endif
        {
            QImageReader reader(node->path);
            QSize size;
            if (!node->size.isEmpty()) {
                size = reader.size();
                size.scale(node->size, Qt::KeepAspectRatio);
                if (!(node->flags & NoSmoothScale))
                    reader.setScaledSize(size);
            }
            if (reader.read(&img) && (node->flags & NoSmoothScale) && !size.isNull()) {
                img = img.scaled(size);
            }
        }
        if (img.isNull()) {
            emit loadError(node->userData);
        } else {
            emit imageLoaded(node->userData, img);
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
    QSet<QString> formats;
    if (!detectFileName) {
        const QList<QByteArray> ba = QImageReader::supportedImageFormats();
        for (int i=0; i<ba.size(); ++i) {
            QString string = QString::fromLocal8Bit(ba.at(i));
            formats.insert(string);
            formats.insert(string.toUpper());
        }
        formats.insert("pdf");
        formats.insert("PDF");
    }

    QDirIterator it(directory, QDir::NoDotAndDotDot|QDir::Files|QDir::Dirs,
                    recurse ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    int index = 0;
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (::matchSize(minSize, maxSize, fi)) {
            const QString absoluteFilePath = fi.absoluteFilePath();
            if (detectFileName) {
                if (matches(absoluteFilePath) && ImageLoaderThread::canLoad(absoluteFilePath)) {
                    emit file(absoluteFilePath);
                }
            } else if (formats.contains(fi.suffix()) && matches(absoluteFilePath)) {
                emit file(absoluteFilePath);
            }
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
    return (!QImageReader::imageFormat(fileName).isEmpty()
            || fileName.endsWith(".pdf", Qt::CaseInsensitive));
}

int ImageLoaderThread::pending() const
{
    QMutexLocker lock(&mMutex);
    return mPending;
}
