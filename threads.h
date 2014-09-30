#ifndef THREADS_H
#define THREADS_H

#include <QtGui>

class ImageLoaderThread : public QThread
{
    Q_OBJECT
public:
    ImageLoaderThread();
    ~ImageLoaderThread();
    void run();
    void abort();
    void clear();

    enum Flag {
        None = 0x0,
        NoSmoothScale = 0x1,
        HighPriority = 0x2
    };

    void load(QImageReader *reader, uint flags, int rotation, void *userData, const QSize &s = QSize());
    bool remove(void *userData);
    static bool canLoad(const QString &fileName);
    int pending() const;
signals:
    void imageLoaded(void *userData, const QImage &image);
    void loadError(void *userData);
private:
    friend class Window;
    mutable QMutex mMutex;
    QWaitCondition mWaitCondition;
    struct Node {
        ~Node() { delete reader; }
        QImageReader *reader;
        QSize size;
        int rotation;
        uint flags;
        Node *next;
        void *userData;
    } *mFirst, *mLast;
    volatile bool mAborted;
    int mPending;
};

class ThumbLoaderThread : public QThread
{
    Q_OBJECT
public:
    ThumbLoaderThread(const QImage &image, int width);
    void run();
signals:
    void thumbLoaded(const QImage &image);
private:
    const QImage original;
    const int width;
};

class FileNameThread : public QThread
{
    Q_OBJECT
public:
    FileNameThread(const QString &dir, /*int min, int max, */const QRegExp &rx, const QRegExp &irx, bool detectFileName, bool recurse);
    void setSizeConstraints(int min, int max);
    void run();
    bool isAborted() const;
    void abort();
signals:
    void file(const QString &file);
private:
    bool matches(const QString &filename) const;
    const QString directory;
    //const int minDepth;
    //const int maxDepth;
    volatile bool aborted;
    mutable QMutex abortMutex;
    const QRegExp regexp, ignore;
    const bool detectFileName;
    const bool recurse;
    int minSize, maxSize;
};


#endif
