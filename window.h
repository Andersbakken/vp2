#ifndef WINDOW_H
#define WINDOW_H
#include <QtGui>
#include <QtNetwork>
#include "threads.h"
#include "flags.h"

struct Data {
    Data() : movie(0), rotation(0), flags(0) {}

    QString path;
    QImage image;
    QMovie *movie;
    int rotation;

    bool clear() {
        if (!image.isNull()) {
            image = QImage();
            return true;
        }
        if (movie) {
            delete movie;
            movie = 0;
            return true;
        }
        return false;
    }

    enum Flag {
        None = 0x0,
        Failed = 0x1,
        Seen = 0x2,
        Network = 0x4
    };
    uint flags;
};

class Window : public QAbstractScrollArea, private Flags
{
    Q_OBJECT
public:
    Window(const QStringList &args, QWidget *parent = 0);
    ~Window();
protected:
    bool event(QEvent *e);
    void mouseMoveEvent(QMouseEvent *e);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    void mouseDoubleClickEvent(QMouseEvent *);
    void contextMenuEvent(QContextMenuEvent *e);
    void wheelEvent(QWheelEvent *e);
    void resizeEvent(QResizeEvent *);
    void paintEvent(QPaintEvent *);
    void showEvent(QShowEvent *e);
    void timerEvent(QTimerEvent *e);
    void keyPressEvent(QKeyEvent *e);
    void closeEvent(QCloseEvent *e);
    virtual void scrollContentsBy(int dx, int dy);
    int indexOf(const QString &string, int index) const;
    int lastIndexOf(const QString &string, int index) const;
    void updateThumbnails() {
        viewport()->update(d.areas[ThumbLeft]);
        viewport()->update(d.areas[ThumbRight]);
    }
    void updateAreas();
    QRect textArea() const;
    //    void zoom(double ratio);
public slots:
    //     void zoomNormal() { zoom(1); }
    //     void zoomIn();
    //     void zoomOut();
    void rotateLeft();
    void rotateRight();
    void ensurePointerHidden();
    void updateImages();
    void about();
    bool searchNext();
    void nextImage();
    void previousImage();
    void nextPage(); // ### poor name
    void previousPage(); // ### poor name
    void home();
    void end();
    void toggleShowThumbnails();
    void toggleShowFileName();
    void startSearch();
    void startRect();
    void toggleCursorVisible();
    void showInfo();
    void copyPath() const;
    bool searchPrevious();
    void onLineEditReturnPressed();
    void addDirectoryRecursively();
    void addDirectory();
    void fileNameThreadFinished();
    void removeCurrentImage();
    void toggleRemoveCurrentImage();
    void undeleteCurrentImage();
    void addImages();
    void clearImages();
    void addFile(const QString &path);
    void addNode(Data *node);
    void toggleSlideShow();
    void toggleAutoZoom();
    void onImageLoadError(void *);
    void onImageLoaded(void *, const QImage &image);
    void onThumbLoaded(const QImage &thumb);
    void debug();
    void onThumbThreadFinished();

    bool purge();
    void resetLineEditStyleSheet();
    void back();
    void forward();
    void onNetworkReplyFinished(QNetworkReply *reply);
private:
    void modifyIndexes(int index, int added);
    void restartQuitTimer();
    void updateScrollBars();
    void nextDirectory(int count);
    void setBackgroundColor(const QString &color);
    void parseArgs(const QStringList &args);
    void addDirectory(const QString &path, bool recurse);
    bool rightSize(const QSize &siz, const QSize &widgetSize) const;
    void load(int index);
    void setCurrentIndex(int index);
    inline int bound(int cnt) const;
    void moveCurrentIndexBy(int count);
    void removeFile(Data *data);

    enum Sort { None, Alphabetically, Size, CreationDate, Random, Natural };
    enum Area { Top, Bottom, TopLeft, ThumbLeft, BottomLeft, Center,
                TopRight, ThumbRight, BottomRight, NumAreas };

    struct ThumbInfo {
        ThumbInfo() : thread(0), requestedWidth(-1) {}
        QImage image;
        ThumbLoaderThread *thread;
        int requestedWidth;
    };

    struct {
        QHash<Data*, int> loading;

        QList<Data*> data;
        QSet<Data*> toDelete;
        int current;

        QLinkedList<int> history;

        double slideShowInterval;
        int maxImages;
        QString indexBuffer;
        QSet<FileNameThread*> fileNameThreads;
        QColor penColor;
        QRegExp regexp, ignoreRegexp;

        QRect areas[NumAreas];

        //  struct ShortCut {
        //      const char *member;
        //      QKeySequence sequence;
        //      const QString description;
        //  } shortcuts[]; // show info of all shortcuts on ?

        QSet<ThumbLoaderThread*> thumbLoaderThreads;
        ThumbInfo thumbLeft, thumbRight;
        int thumbMinWidth;

        Sort sort;
        QString longestPath;
        int fontSize;
        QBasicTimer updateFontSizeTimer, quitTimer, updateImagesTimer, slideShowTimer,
            indexBufferTimer, updateScrollBarsTimer, indexBufferClearTimer;
        QLineEdit *lineEdit;
        bool search;
        int maxThreads;
        int minSize, maxSize;
        double quitTimerMinutes;
        int imagesInMemory;
        QNetworkAccessManager *networkManager;
        ImageLoaderThread imageLoaderThread;
        QPoint pressPosition;
        bool midButtonPressed;
        QVector<QRect> rects;
    } d;
};


#endif
