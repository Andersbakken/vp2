#include "window.h"

static QDir backupDir()
{
    QDir dir = QDir::home();
#ifdef Q_OS_UNIX
    dir.mkdir(".vp2bak");
    dir.cd(".vp2bak");
#else
    dir.mkdir("_vp2bak");
    dir.cd("_vp2bak");
#endif
    return dir;
}

static inline QSet<int> surrounding(int cur, int count, int maxEntries)
{
    QSet<int> ret;
    if (count == 0)
        return ret;
    if (cur == -1)
        cur = 0;
    maxEntries = qMin(maxEntries, count);

    int above = (maxEntries * 2) / 3;
    int below = maxEntries - above;
    for (int i=0; i<above; ++i) {
        int index = cur + i + 1;
        if (index >= count) {
            index -= count;
            if (ret.contains(index)) {
                return ret;
            }
        }
        ret.insert(index);
    }

    for (int i=0; i<below; ++i) {
        int index = cur - (i + 1);
        if (index < 0) {
            index += count;
            if (ret.contains(index)) {
                break;
            }
        }
        ret.insert(index);
    }

    return ret;
}

Window::Window(const QStringList &args, QWidget *parent)
    : QAbstractScrollArea(parent), Flags(FirstImage|DisplayThumbnails)
{
    d.current = -1;
    resetCycleCursors();
    d.slideShowInterval = 3;
    d.maxImages = 30;
    d.penColor = Qt::yellow;
    d.thumbMinWidth = 50;
    d.fontSize = -1;
    d.maxThreads = 3;
    d.minSize = -1;
    d.maxSize = -1;
    d.midButtonPressed = false;
    d.quitTimerMinutes = 5;
    d.networkManager = 0;
    d.imagesInMemory = 0;
    d.sort = None;
#ifdef VIDEO_ENABLED
    d.videoDecoder = 0;
    d.videoDecoderOwner = 0;
    d.videoPaused = false;
    d.videoSeekSeconds = 5.0;
    d.videoControls = 0;
    d.playPauseButton = 0;
    d.skipBackButton = 0;
    d.skipForwardButton = 0;
    d.positionSlider = 0;
    d.positionSliderPressed = false;
#endif

    //    setViewport(new Viewport(this));
    d.lineEdit = new QLineEdit(this);
    d.search = true;
    new QShortcut(QKeySequence(Qt::Key_Escape), d.lineEdit, SLOT(hide()));
    //     new QShortcut(QKeySequence(Qt::Key_F6), this, SLOT(debug()));

    connect(d.lineEdit, SIGNAL(returnPressed()), this, SLOT(onLineEditReturnPressed()));
    d.lineEdit->installEventFilter(this);
    d.lineEdit->hide();

    setMouseTracking(true);
    d.longestPath = QLatin1String("No Images Specified");
    set(DisplayFileName, QSettings().value("displayFileName", false).toBool());
    set(DisplayThumbnails, QSettings().value("displayThumbnails", false).toBool());
    set(HidePointer, QSettings().value("hidePointer", false).toBool());
    set(AutoZoomEnabled, QSettings().value("autoZoom", true).toBool());

    setBackgroundColor(QSettings().value("bgcol", "grid").toString().toLower());
    createActions();
#ifdef VIDEO_ENABLED
    createVideoControls();
#endif
    parseArgs(args);

    const QList<QFileInfo> files = backupDir().entryInfoList(QDir::Files|QDir::NoDotAndDotDot);
    if (!files.isEmpty()) {
        const QDateTime current = QDateTime::currentDateTime();
        foreach(const QFileInfo &fi, files) {
            if (current.secsTo(fi.birthTime()) >= 3600 * 24) {
                QFile::remove(fi.absoluteFilePath());
                qDebug() << "Actually removing" << fi.absoluteFilePath();
            }
        }
    }
    connect(&d.imageLoaderThread, SIGNAL(imageLoaded(void*, QImage, QSize)),
            this, SLOT(onImageLoaded(void *, QImage, QSize)));
    connect(&d.imageLoaderThread, SIGNAL(loadError(void*)),
            this, SLOT(onImageLoadError(void *)));
    d.imageLoaderThread.start();
}

namespace {
QAction *makeAction(QWidget *parent, const QString &text,
                    const QList<QKeySequence> &shortcuts)
{
    QAction *a = new QAction(text, parent);
    a->setShortcuts(shortcuts);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    parent->addAction(a);
    return a;
}

QAction *makeAction(QWidget *parent, const QString &text,
                    const QKeySequence &shortcut)
{
    return makeAction(parent, text, QList<QKeySequence>() << shortcut);
}

QAction *makeAction(QWidget *parent, const QString &text)
{
    return makeAction(parent, text, QList<QKeySequence>());
}
}

void Window::createActions()
{
    Actions &a = d.act;

    a.nextImage = makeAction(this, tr("Next image"),
                             QList<QKeySequence>()
                             << QKeySequence(Qt::Key_Space)
                             << QKeySequence(Qt::Key_Right)
                             << QKeySequence(Qt::Key_Down));
    connect(a.nextImage, SIGNAL(triggered()), this, SLOT(nextImage()));

    a.previousImage = makeAction(this, tr("Previous image"),
                                 QList<QKeySequence>()
                                 << QKeySequence(Qt::SHIFT | Qt::Key_Space)
                                 << QKeySequence(Qt::Key_Left)
                                 << QKeySequence(Qt::Key_Up));
    connect(a.previousImage, SIGNAL(triggered()), this, SLOT(previousImage()));

    a.nextPage = makeAction(this, tr("Next image (+10)"),
                            QList<QKeySequence>()
                            << QKeySequence(Qt::Key_Greater)
                            << QKeySequence(Qt::CTRL | Qt::Key_Right)
                            << QKeySequence(Qt::CTRL | Qt::Key_Down));
    connect(a.nextPage, SIGNAL(triggered()), this, SLOT(nextPage()));

    a.previousPage = makeAction(this, tr("Previous image (-10)"),
                                QList<QKeySequence>()
                                << QKeySequence(Qt::Key_Less)
                                << QKeySequence(Qt::CTRL | Qt::Key_Left)
                                << QKeySequence(Qt::CTRL | Qt::Key_Up));
    connect(a.previousPage, SIGNAL(triggered()), this, SLOT(previousPage()));

    a.home = makeAction(this, tr("First image"), QKeySequence(Qt::Key_Home));
    connect(a.home, SIGNAL(triggered()), this, SLOT(home()));

    a.end = makeAction(this, tr("Last image"), QKeySequence(Qt::Key_End));
    connect(a.end, SIGNAL(triggered()), this, SLOT(end()));

    a.back = makeAction(this, tr("Back"), QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(a.back, SIGNAL(triggered()), this, SLOT(back()));

    a.forward = makeAction(this, tr("Forward"), QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(a.forward, SIGNAL(triggered()), this, SLOT(forward()));

    a.nextDirectory = makeAction(this, tr("Next directory"),
                                 QList<QKeySequence>()
                                 << QKeySequence(Qt::CTRL | Qt::Key_Space)
                                 << QKeySequence(Qt::ALT | Qt::Key_Space));
    connect(a.nextDirectory, &QAction::triggered, this, [this]() { nextDirectory(1); });

    a.previousDirectory = makeAction(this, tr("Previous directory"),
                                     QList<QKeySequence>()
                                     << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space)
                                     << QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Space));
    connect(a.previousDirectory, &QAction::triggered, this, [this]() { nextDirectory(-1); });

    a.shuffleAll = makeAction(this, tr("Shuffle all"),
                              QKeySequence(Qt::SHIFT | Qt::Key_Z));
    connect(a.shuffleAll, SIGNAL(triggered()), this, SLOT(shuffle()));

    a.shufflePrev = makeAction(this, tr("Shuffle previous image"),
                               QKeySequence(Qt::SHIFT | Qt::Key_1));
    connect(a.shufflePrev, SIGNAL(triggered()), this, SLOT(shufflePrev()));

    a.shuffleCenter = makeAction(this, tr("Shuffle current image"),
                                 QKeySequence(Qt::SHIFT | Qt::Key_2));
    connect(a.shuffleCenter, SIGNAL(triggered()), this, SLOT(shuffleCenter()));

    a.shuffleNext = makeAction(this, tr("Shuffle next image"),
                               QKeySequence(Qt::SHIFT | Qt::Key_3));
    connect(a.shuffleNext, SIGNAL(triggered()), this, SLOT(shuffleNext()));

    a.cyclePrevForward = makeAction(this, tr("Cycle previous image forward"),
                                    QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(a.cyclePrevForward, SIGNAL(triggered()), this, SLOT(cyclePrevForward()));

    a.cyclePrevBackward = makeAction(this, tr("Cycle previous image backward"),
                                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1));
    connect(a.cyclePrevBackward, SIGNAL(triggered()), this, SLOT(cyclePrevBackward()));

    a.cycleCenterForward = makeAction(this, tr("Cycle current image forward"),
                                      QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(a.cycleCenterForward, SIGNAL(triggered()), this, SLOT(cycleCenterForward()));

    a.cycleCenterBackward = makeAction(this, tr("Cycle current image backward"),
                                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2));
    connect(a.cycleCenterBackward, SIGNAL(triggered()), this, SLOT(cycleCenterBackward()));

    a.cycleNextForward = makeAction(this, tr("Cycle next image forward"),
                                    QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(a.cycleNextForward, SIGNAL(triggered()), this, SLOT(cycleNextForward()));

    a.cycleNextBackward = makeAction(this, tr("Cycle next image backward"),
                                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_3));
    connect(a.cycleNextBackward, SIGNAL(triggered()), this, SLOT(cycleNextBackward()));

    a.toggleVideoPlayback = makeAction(this, tr("Play/pause video"),
                                       QList<QKeySequence>()
                                       << QKeySequence(Qt::Key_P)
                                       << QKeySequence(Qt::Key_Return)
                                       << QKeySequence(Qt::Key_Enter));
    connect(a.toggleVideoPlayback, SIGNAL(triggered()),
            this, SLOT(toggleVideoPlayback()));

    a.videoSeekForward = makeAction(this, tr("Seek forward"),
                                    QKeySequence(Qt::SHIFT | Qt::Key_Right));
    connect(a.videoSeekForward, SIGNAL(triggered()),
            this, SLOT(videoSeekForward()));

    a.videoSeekBackward = makeAction(this, tr("Seek backward"),
                                     QKeySequence(Qt::SHIFT | Qt::Key_Left));
    connect(a.videoSeekBackward, SIGNAL(triggered()),
            this, SLOT(videoSeekBackward()));

    a.randomImage = makeAction(this, tr("Random image"), QKeySequence(Qt::Key_Z));
    connect(a.randomImage, SIGNAL(triggered()), this, SLOT(randomImage()));

    a.rotateLeft = makeAction(this, tr("Rotate left"),
                              QKeySequence(Qt::Key_BracketLeft));
    connect(a.rotateLeft, SIGNAL(triggered()), this, SLOT(rotateLeft()));

    a.rotateRight = makeAction(this, tr("Rotate right"),
                               QKeySequence(Qt::Key_BracketRight));
    connect(a.rotateRight, SIGNAL(triggered()), this, SLOT(rotateRight()));

    a.toggleRemove = makeAction(this, tr("Delete image"),
                                QList<QKeySequence>()
                                << QKeySequence(Qt::Key_Delete)
                                << QKeySequence(Qt::Key_Backspace)
                                << QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(a.toggleRemove, SIGNAL(triggered()), this, SLOT(toggleRemoveCurrentImage()));

    a.undelete = makeAction(this, tr("Undelete current image"),
                            QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(a.undelete, SIGNAL(triggered()), this, SLOT(undeleteCurrentImage()));

    a.purge = makeAction(this, tr("Purge removed images"));
    connect(a.purge, SIGNAL(triggered()), this, SLOT(purge()));

    a.startSearch = makeAction(this, tr("Search"), QKeySequence(Qt::Key_Slash));
    connect(a.startSearch, SIGNAL(triggered()), this, SLOT(startSearch()));

    a.searchNext = makeAction(this, tr("Find next"), QKeySequence(Qt::Key_N));
    connect(a.searchNext, SIGNAL(triggered()), this, SLOT(searchNext()));

    a.searchPrevious = makeAction(this, tr("Find previous"),
                                  QKeySequence(Qt::SHIFT | Qt::Key_N));
    connect(a.searchPrevious, SIGNAL(triggered()), this, SLOT(searchPrevious()));

    a.randomSearchNext = makeAction(this, tr("Random search next"),
                                    QKeySequence(Qt::CTRL | Qt::Key_Z));
    connect(a.randomSearchNext, SIGNAL(triggered()), this, SLOT(randomSearchNext()));

    a.toggleAutoZoom = makeAction(this, tr("Toggle autozoom"));
    connect(a.toggleAutoZoom, SIGNAL(triggered()), this, SLOT(toggleAutoZoom()));

    a.toggleShowFileName = makeAction(this, tr("Show file name"),
                                      QKeySequence(Qt::Key_T));
    connect(a.toggleShowFileName, SIGNAL(triggered()), this, SLOT(toggleShowFileName()));

    a.toggleShowThumbnails = makeAction(this, tr("Show thumbnails"),
                                        QKeySequence(Qt::Key_H));
    connect(a.toggleShowThumbnails, SIGNAL(triggered()), this, SLOT(toggleShowThumbnails()));

    a.toggleCursor = makeAction(this, tr("Hide cursor"), QKeySequence(Qt::Key_C));
    connect(a.toggleCursor, SIGNAL(triggered()), this, SLOT(toggleCursorVisible()));

    a.toggleSlideShow = makeAction(this, tr("Start slideshow"),
                                   QKeySequence(Qt::Key_S));
    connect(a.toggleSlideShow, SIGNAL(triggered()), this, SLOT(toggleSlideShow()));

    a.cyclePenColor = makeAction(this, tr("Cycle pen color"),
                                 QKeySequence(Qt::SHIFT | Qt::Key_T));
    connect(a.cyclePenColor, SIGNAL(triggered()), this, SLOT(cyclePenColor()));

    a.showInfo = makeAction(this, tr("Show info"),
                            QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(a.showInfo, SIGNAL(triggered()), this, SLOT(showInfo()));

    a.copyPath = makeAction(this, tr("Copy path"),
                            QKeySequence(Qt::CTRL | Qt::Key_C));
    connect(a.copyPath, SIGNAL(triggered()), this, SLOT(copyPath()));

    a.printPath = makeAction(this, tr("Print path to stdout"),
                             QKeySequence(Qt::SHIFT | Qt::Key_F));
    connect(a.printPath, SIGNAL(triggered()), this, SLOT(printPath()));

    a.startRect = makeAction(this, tr("Draw rectangle"),
                             QKeySequence(Qt::SHIFT | Qt::Key_R));
    connect(a.startRect, SIGNAL(triggered()), this, SLOT(startRect()));

    a.about = makeAction(this, tr("About vp2"));
    connect(a.about, SIGNAL(triggered()), this, SLOT(about()));

    a.addImages = makeAction(this, tr("Add files..."),
                             QList<QKeySequence>()
                             << QKeySequence(Qt::Key_O)
                             << QKeySequence(Qt::Key_L));
    connect(a.addImages, SIGNAL(triggered()), this, SLOT(addImages()));

    a.addDirectory = makeAction(this, tr("Add directory..."), QKeySequence(Qt::Key_D));
    connect(a.addDirectory, SIGNAL(triggered()), this, SLOT(addDirectory()));

    a.addDirectoryRecursively = makeAction(this, tr("Add directory recursively..."),
                                           QKeySequence(Qt::ALT | Qt::Key_R));
    connect(a.addDirectoryRecursively, SIGNAL(triggered()),
            this, SLOT(addDirectoryRecursively()));

    a.slideshowFaster = makeAction(this, tr("Slideshow faster"),
                                   QList<QKeySequence>()
                                   << QKeySequence(Qt::Key_Plus)
                                   << QKeySequence(Qt::SHIFT | Qt::Key_Equal));
    connect(a.slideshowFaster, SIGNAL(triggered()), this, SLOT(slideshowFaster()));

    a.slideshowSlower = makeAction(this, tr("Slideshow slower"),
                                   QKeySequence(Qt::Key_Minus));
    connect(a.slideshowSlower, SIGNAL(triggered()), this, SLOT(slideshowSlower()));

    a.showNormal = makeAction(this, tr("Show normal"), QKeySequence(Qt::Key_R));
    connect(a.showNormal, SIGNAL(triggered()), this, SLOT(showNormalSlot()));

    a.showMaximized = makeAction(this, tr("Show maximized"), QKeySequence(Qt::Key_X));
    connect(a.showMaximized, SIGNAL(triggered()), this, SLOT(showMaximizedSlot()));

    a.showFullScreen = makeAction(this, tr("Show full screen"));
    connect(a.showFullScreen, &QAction::triggered, this, [this]() { showFullScreen(); });

    a.toggleFullScreen = makeAction(this, tr("Toggle full screen"),
                                    QKeySequence(Qt::Key_F));
    connect(a.toggleFullScreen, SIGNAL(triggered()), this, SLOT(toggleFullScreen()));

    a.quit = makeAction(this, tr("Quit"), QKeySequence(Qt::Key_Q));
    connect(a.quit, SIGNAL(triggered()), this, SLOT(quitSlot()));
}

Window::~Window()
{
#ifdef VIDEO_ENABLED
    delete d.videoDecoder;
    d.videoDecoder = 0;
    d.videoDecoderOwner = 0;
#endif
    d.imageLoaderThread.abort();
    d.imageLoaderThread.wait();
    qDeleteAll(d.data);
}

void Window::setBackgroundColor(const QString &string)
{
    QBrush brush;
    if (string == tr("grid")) {
        QImage im(40, 40, QImage::Format_RGB32);
        im.fill(QColor(Qt::darkGray).rgba());
        QPainter p(&im);
        p.fillRect(20, 0, 20, 20, Qt::gray);
        p.fillRect(0, 20, 20, 20, Qt::gray);
        p.end();
        brush = im;
    } else {
        QColor color(string);
        brush = color;
    }

    QPalette pal = viewport()->palette();
    pal.setBrush(viewport()->backgroundRole(), brush);
    viewport()->setPalette(pal);
    viewport()->setAutoFillBackground(true);
}

static inline int findPoint(const QPoint &p, const QRect *rects, int size)
{
    for (int i=0; i<size; ++i) {
        if (rects[i].contains(p))
            return i;
    }

    return -1;
}

bool Window::event(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange && test(HidePointer)) {
        QTimer::singleShot(100, this, SLOT(ensurePointerHidden()));
    }

    return QAbstractScrollArea::event(e);
}

bool Window::eventFilter(QObject *watched, QEvent *e)
{
    // While the search/rect line edit has focus, it owns all key input. Accept
    // ShortcutOverride for every key so Qt's shortcut machinery doesn't fire
    // window QActions (like N for searchNext, Return for toggleVideoPlayback,
    // or plain letters that would otherwise invoke view toggles) while the
    // user is typing a search pattern.
    if (watched == d.lineEdit && e->type() == QEvent::ShortcutOverride) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(e);
        if (ke->key() != Qt::Key_Escape) {
            ke->accept();
            return true;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, e);
}


void Window::mouseMoveEvent(QMouseEvent *e)
{
    restartQuitTimer();
    if (d.midButtonPressed) {
        viewport()->update();
    }

    QAbstractScrollArea::mouseMoveEvent(e);
}

void Window::mouseDoubleClickEvent(QMouseEvent *e)
{
    mousePressEvent(e);
}

void Window::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (d.data.isEmpty()) {
            addImages();
        } else if (d.data.size() > 1) {
            if (e->modifiers() & Qt::ShiftModifier || (e->x() < viewport()->width() / 2)) {
                moveCurrentIndexBy(-1);
            } else {
                moveCurrentIndexBy(1);
            }
        }
        e->accept();
    } else {
        if (e->button() == Qt::MiddleButton) {
            d.midButtonPressed = true;
            d.pressPosition = e->pos();
            viewport()->update();
        }
        QAbstractScrollArea::mousePressEvent(e);
    }
}

void Window::mouseReleaseEvent(QMouseEvent *)
{
    if (d.midButtonPressed) {
        d.midButtonPressed = false;
        viewport()->update();
    }
}

void Window::refreshActionLabels()
{
    const Actions &a = d.act;
    a.toggleShowFileName->setText(test(DisplayFileName)
                                  ? tr("Hide file name") : tr("Show file name"));
    a.toggleShowThumbnails->setText(test(DisplayThumbnails)
                                    ? tr("Hide thumbnails") : tr("Show thumbnails"));
    a.toggleCursor->setText(test(HidePointer)
                            ? tr("Show cursor") : tr("Hide cursor"));
    a.toggleAutoZoom->setText(test(AutoZoomEnabled)
                              ? tr("Turn off autozoom") : tr("Turn on autozoom"));
    a.toggleSlideShow->setText(d.slideShowTimer.isActive()
                               ? tr("Stop slideshow") : tr("Start slideshow"));

    const bool haveCurrent = !d.data.isEmpty() && d.current != -1;
    const bool isRemoved = haveCurrent && d.toDelete.contains(d.data.at(d.current));
    a.toggleRemove->setText(isRemoved ? tr("Undelete image") : tr("Delete image"));
    a.toggleRemove->setEnabled(haveCurrent);
    a.undelete->setEnabled(haveCurrent && isRemoved);
    a.purge->setEnabled(!d.toDelete.isEmpty());
    a.copyPath->setEnabled(haveCurrent);
    a.printPath->setEnabled(haveCurrent);
    a.rotateLeft->setEnabled(haveCurrent);
    a.rotateRight->setEnabled(haveCurrent);
    a.startRect->setEnabled(haveCurrent);
    a.showInfo->setEnabled(!d.data.isEmpty());

    const bool multi = d.data.size() > 1;
    a.nextImage->setEnabled(multi);
    a.previousImage->setEnabled(multi);
    a.nextPage->setEnabled(multi);
    a.previousPage->setEnabled(multi);
    a.home->setEnabled(multi);
    a.end->setEnabled(multi);
    a.shuffleAll->setEnabled(multi);
    a.randomImage->setEnabled(multi);
    const bool fourPlus = d.data.size() >= 4;
    a.shufflePrev->setEnabled(fourPlus);
    a.shuffleCenter->setEnabled(fourPlus);
    a.shuffleNext->setEnabled(fourPlus);
    a.cyclePrevForward->setEnabled(fourPlus);
    a.cyclePrevBackward->setEnabled(fourPlus);
    a.cycleCenterForward->setEnabled(fourPlus);
    a.cycleCenterBackward->setEnabled(fourPlus);
    a.cycleNextForward->setEnabled(fourPlus);
    a.cycleNextBackward->setEnabled(fourPlus);
    a.nextDirectory->setEnabled(multi);
    a.previousDirectory->setEnabled(multi);
    a.back->setEnabled(!d.history.isEmpty());
    a.forward->setEnabled(!d.history.isEmpty());

    const Qt::WindowStates ws = windowState();
    a.showNormal->setEnabled(ws & (Qt::WindowFullScreen | Qt::WindowMaximized));
    a.showMaximized->setEnabled(!(ws & Qt::WindowMaximized));
    a.showFullScreen->setEnabled(!(ws & Qt::WindowFullScreen));

    const bool hasCurrentVideo = haveCurrent
        && (d.data.at(d.current)->flags & Data::Video);
    a.toggleVideoPlayback->setEnabled(hasCurrentVideo);
    a.videoSeekForward->setEnabled(hasCurrentVideo);
    a.videoSeekBackward->setEnabled(hasCurrentVideo);
    bool playing = false;
#ifdef VIDEO_ENABLED
    playing = hasCurrentVideo && d.videoDecoder && !d.videoPaused;
#endif
    a.toggleVideoPlayback->setText(playing ? tr("Pause video") : tr("Play video"));
}

void Window::contextMenuEvent(QContextMenuEvent *e)
{
    refreshActionLabels();

    QMenu menu(this);
    const Actions &a = d.act;

    menu.addAction(a.nextImage);
    menu.addAction(a.previousImage);
    menu.addAction(a.home);
    menu.addAction(a.end);
    menu.addAction(a.back);
    menu.addAction(a.forward);
    menu.addAction(a.nextDirectory);
    menu.addAction(a.previousDirectory);
    menu.addSeparator();

    menu.addAction(a.shuffleAll);
    menu.addAction(a.shufflePrev);
    menu.addAction(a.shuffleCenter);
    menu.addAction(a.shuffleNext);
    menu.addAction(a.cyclePrevForward);
    menu.addAction(a.cyclePrevBackward);
    menu.addAction(a.cycleCenterForward);
    menu.addAction(a.cycleCenterBackward);
    menu.addAction(a.cycleNextForward);
    menu.addAction(a.cycleNextBackward);
    menu.addAction(a.randomImage);
    menu.addSeparator();

    menu.addAction(a.rotateLeft);
    menu.addAction(a.rotateRight);
    menu.addAction(a.toggleRemove);
    menu.addAction(a.purge);
    menu.addSeparator();

    menu.addAction(a.toggleVideoPlayback);
    menu.addAction(a.videoSeekBackward);
    menu.addAction(a.videoSeekForward);
    menu.addSeparator();

    menu.addAction(a.startSearch);
    menu.addAction(a.searchNext);
    menu.addAction(a.searchPrevious);
    menu.addSeparator();

    menu.addAction(a.toggleShowFileName);
    menu.addAction(a.toggleShowThumbnails);
    menu.addAction(a.toggleCursor);
    menu.addAction(a.toggleAutoZoom);
    menu.addAction(a.toggleSlideShow);
    menu.addAction(a.cyclePenColor);
    menu.addSeparator();

    menu.addAction(a.copyPath);
    if (!d.data.isEmpty() && d.current != -1) {
        a.copyPath->setText(tr("Copy path: '%1'").arg(d.data.at(d.current)->path));
    } else {
        a.copyPath->setText(tr("Copy path"));
    }
    menu.addAction(a.printPath);
    menu.addAction(a.showInfo);
    menu.addAction(a.startRect);
    menu.addSeparator();

    menu.addAction(a.addImages);
    menu.addAction(a.addDirectory);
    menu.addAction(a.addDirectoryRecursively);
    menu.addSeparator();

    menu.addAction(a.slideshowFaster);
    menu.addAction(a.slideshowSlower);
    menu.addSeparator();

    menu.addAction(a.showNormal);
    menu.addAction(a.showMaximized);
    menu.addAction(a.showFullScreen);
    menu.addSeparator();

    QMenu *colorMenu = menu.addMenu(tr("Background color"));
    static const char *const colors[] = { "Grid", "Black", "Red", "Green", "Blue",
                                          "Yellow", "Gray", 0 };
    for (int i = 0; colors[i]; ++i) {
        colorMenu->addAction(tr(colors[i]));
    }

    menu.addAction(a.about);
    menu.addSeparator();
    menu.addAction(a.quit);

    QAction *ret = menu.exec(e->globalPos());
    if (ret && ret->parent() == colorMenu) {
        const QString name = ret->text().toLower();
        QSettings().setValue("bgcol", name);
        setBackgroundColor(name);
    }
}

struct Pic {
    QString path;
    QUrl url;
    enum PicType {
        File,
        Dir,
        Network
    } type;
};

enum Type {
    Help,
    Slideshow,
    Fullscreen,
    ShowNormal,
    Randomize,
    Sort,
    DetectFileType,
    Color,
    DisplayFileName,
    HideFileName,
    DisplayThumbnails,
    XErrorKludge,
    HidePointer,
    Name,
    IName,
    Ignore,
    IIgnore,
    Exclude,
    Opacity,
    QuitTimer,
    AutoZoom,
    Recurse,
    MaxImageCount,
    MaxThreadCount,
    DashDash,
    //MaxDepth,
    //MinDepth,
    Dash,
    MaxSize,
    MinSize,
    IgnoreFailed,
    NoSmoothScale,
    BypassX11,
    NumTypes
};

void Window::parseArgs(const QStringList &argsIn)
{
    QStringList args = argsIn;
    // Split --option=value into --option value so the main parser below
    // can read the value as the following positional argument.
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (!arg.startsWith("--")) {
            continue;
        }
        const int eq = arg.indexOf('=');
        if (eq <= 2) {
            continue;
        }
        const QString opt = arg.left(eq);
        const QString val = arg.mid(eq + 1);
        args.replace(i, opt);
        args.insert(i + 1, val);
    }
    const QRegExp multi("^-[A-Za-z][A-Za-z]+$");
    for (int i=1; i<args.size(); ++i) {
        if (multi.exactMatch(args.at(i))) {
            const QString arg = args.takeAt(i);
            for (int j=1; j<arg.size(); ++j) {
                args.insert(i + j - 1, QString("-%1").arg(arg.at(j)));
            }
            i += arg.size() - 2;
        }
    }


    enum ExtraArg {
        No,
        One,
        Two,
        Optional
    };

    struct {
        const char *shortOpt, *longOpt;
        const Type type;
        const ExtraArg extraArg;
        const char *description; // translate?
    } options[] = {
        { "-h", "--help", ::Help, No, "Display this help" },
        { "-s", "--slideshow", ::Slideshow, Optional, "Start slideshow (optional seconds argument)" },
        { "-f", "--fullscreen", ::Fullscreen, No, "Display full screen" },
        { 0, "--show-normal", ::ShowNormal, No, "Show normal" },
        { "-z", "--randomize", ::Randomize, No, "Randomize order of images, same as --sort random" },
        { "-o", "--sort", ::Sort, One, "Set sorting (size|s, filename|f, random|r, creationdate|d, natural|n)" },
        { 0, "--detect-filetype", ::DetectFileType, No, "Detect file type (don't trust extension)" },
        { 0, "--backgroundcolor", ::Color, One, "Background color. E.g. --backgroundcolor red" },
        { 0, "--display-file-name", ::DisplayFileName, No, "Display file name" },
        { 0, "--hide-file-name", ::DisplayFileName, No, "Hide file name" },
        { 0, "--display-thumbnails", ::DisplayFileName, No, "Display thumbnails" },
        { 0, "--hide-thumbnails", ::DisplayFileName, No, "Hide thumbnails" },
        { 0, "--xerror-kludge", ::XErrorKludge, No, "Use this if you have problems with background painting" },
        { "-p", "--hide-pointer", ::HidePointer, No, "Hide pointer" },
        { "-n", "--name", ::Name, One, "Load only files matching arg in directories (case sensitive)" },
        { "-u", "--iname", ::IName, One, "Load only files matching arg in directories (case insensitive)" },
        { 0, "--ignore", ::Ignore, One, "Don't load files matching arg in directories (case sensitive)" },
        { 0, "--iignore", ::Ignore, One, "Don't load files matching arg in directories (case insensitive)" },
        { 0, "--exclude", ::Exclude, One, "Exclude files matching arg (wildcard by default, or /regex/)" },
        //{ 0, "--maxdepth", ::MaxDepth, One, "Max recursion depth" },
        //{ 0, "--mindepth", ::MinDepth, One, "Min recursion depth" },
        { 0, "--opacity", ::Opacity, One, "Set opacity of window (in percentage)" },
        { 0, "--quit-timer", ::QuitTimer, One, "Quit after [arg] minutes of inactivity (default 5). 0 means disable" },
        { "-Z", "--auto-zoom", ::AutoZoom, No, "Auto zoom" },
        { "-r", "--recurse", ::Recurse, No, "Recurse subdirectories" },
        { 0, "--max-images", ::MaxImageCount, One, "Limit number of images to keep in memory to argument" },
        { 0, "--max-threads", ::MaxThreadCount, One, "Limit number of threads to run concurrently to argument" },
        { 0, "--max-size", ::MaxSize, One, "Don't load images that are larger than [arg] kb" },
        { 0, "--min-size", ::MinSize, One, "Only load images that are larger than or equal to [arg] kb" },
        { 0, "--ignore-failed", ::IgnoreFailed, No, "Ignore images that fail to load" },
        { 0, "--bypass-x11", ::BypassX11, No, "Bypass X11 window management" },
        { 0, "--no-smoothscale", ::NoSmoothScale, No, "Don't smoothscale images" },
        { 0, "-", ::Dash, No, "Read pictures/directories from stdin" },
        { 0, "--", ::DashDash, No, "Treat everything after this argument as file names or directories" },
        { 0, 0, ::NumTypes, No, 0 }
    };

    enum {
        ShowFullScreen = 0x01,
        RecurseDirs = 0x02,
        SlideShow = 0x04,
        ShowHelp = 0x08,
        SeenDashDash = 0x20
    };
    //int minDepth = 1, maxDepth = INT_MAX;
    QString errorMessage;
    uint status = 0;

    QList<Pic> pictures;
    for (int i=1; i<args.size(); ++i) {
        const QString arg = args.at(i);
        if (status & SeenDashDash || !arg.startsWith("-")) {
            const QFileInfo fi(arg);
            Pic pic;
            if (!fi.exists()) {
                const QUrl url(arg);
                const QString scheme = url.scheme();
                if (scheme == QLatin1String("http") || scheme == QLatin1String("ftp")) {
                    pic.url = url;
                    pic.type = Pic::Network;
                } else {
                    errorMessage = QString("'%1' doesn't seem to exist").arg(arg);
                    break;
                }
            } else {
                pic.type = fi.isDir() ? Pic::Dir : Pic::File;
                pic.path = fi.absoluteFilePath();
            }
            pictures.append(pic);
        } else {
            int option = 0;
            while (options[option].description) {
                if (arg == options[option].shortOpt || arg == options[option].longOpt) {
                    break;
                }
                ++option;
            }

            if (options[option].description) {
                switch (options[option].extraArg) {
                case No:
                    break;
                case One:
                    if (i + 1 >= args.size()) {
                        errorMessage = QString("'%1' requires an extra argument").arg(arg);
                    }
                    break;
                case Two:
                    if (i + 2 >= args.size()) {
                        errorMessage = QString("'%1' requires two extra arguments").arg(arg);
                    }
                    break;
                case Optional:
                    break;
                }
            }

            if (!errorMessage.isEmpty())
                break;

            switch (options[option].type) {
            case ::NumTypes:
                errorMessage = QString("Unrecognized option: '%1'").arg(arg);
                break;
            case ::Help:
                status |= ShowHelp;
                break;
            case ::Dash: {
                QFile file;
                file.open(stdin, QIODevice::ReadOnly);
                while (!file.atEnd()) {
                    QString line = file.readLine();
                    if (line.isEmpty())
                        break;
                    if (line.endsWith("\n"))
                        line.chop(1);
                    const QFileInfo fi(line);
                    Pic pic;
                    if (!fi.exists()) {
                        const QUrl url(arg);
                        const QString scheme = url.scheme();
                        if (scheme == QLatin1String("http") || scheme == QLatin1String("ftp")) {
                            pic.url = url;
                            pic.type = Pic::Network;
                        } else {
                            errorMessage = QString("'%1' doesn't seem to exist").arg(arg);
                            break;
                        }
                    } else {
                        pic.type = fi.isDir() ? Pic::Dir : Pic::File;
                        pic.path = fi.absoluteFilePath();
                    }
                    pictures.append(pic);
                }
                break;
            }
            case ::NoSmoothScale:
                set(NoSmoothScale);
                break;
            case ::Slideshow:
                if (i + 1 < args.size()) {
                    const QString a = args.at(i + 1);
                    bool ok;
                    const double val = a.toDouble(&ok);
                    if (ok) {
                        ++i;
                        d.slideShowInterval = val;
                    }
                }
                if (!d.slideShowTimer.isActive())
                    toggleSlideShow();
                break;
            case ::Fullscreen:
                status |= ShowFullScreen;
                break;
            case ::ShowNormal:
                status &= ~ShowFullScreen;
                break;
            case ::BypassX11:
                setWindowFlags(windowFlags() | Qt::X11BypassWindowManagerHint);
                break;

            case ::Randomize:
                d.sort = Random;
                break;

            case ::MaxSize:
            case ::MinSize: {
                const QString value = args.at(++i);
                const int kb = value.toInt();
                if (kb <= 0) {
                    errorMessage = QString("%1 must be a positive integer").
                        arg(options[option].type == MaxSize
                            ? "--max-size"
                            : "--min-size");
                    break;
                }
                int &val = (options[option].type == MaxSize ? d.maxSize : d.minSize);
                val = kb;
                if (d.minSize != -1 && d.maxSize != -1 && d.maxSize < d.minSize) {
                    errorMessage = "impossible --max-size/--min-size combination";
                    break;

                }
                break;
            }

            case ::Sort: {
                const QString value = args.at(++i);
                if (value == "s" || value == "size") {
                    d.sort = Size;
                } else if (value == "f" || value == "filename")  {
                    d.sort = Alphabetically;
                } else if (value == "n" || value == "natural")  {
                    d.sort = Natural;
                } else if (value == "d" || value == "creationdate")  {
                    d.sort = CreationDate;
                } else if (value == "r" || value == "random") {
                    d.sort = Random;
                } else {
                    errorMessage = QString("Unrecognized sorting type: '%1'").arg(value);
                }
                break;
            }
            case ::DetectFileType:
                set(DetectFileType);
                break;
            case ::Color:
                setBackgroundColor(args.at(++i));
                break;
            case ::DisplayFileName:
                set(DisplayFileName);
                break;
            case ::HideFileName:
                unset(DisplayFileName);
                break;
            case ::DisplayThumbnails:
                set(DisplayThumbnails);
                break;
            case ::XErrorKludge:
                set(XKludge);
                break;
            case ::HidePointer:
                set(HidePointer);
                break;
            case ::IgnoreFailed:
                set(IgnoreFailed);
                break;
            case ::Name:
            case ::IName: {
                d.regexp.setPattern(args.at(++i));
                d.regexp.setPatternSyntax(QRegExp::Wildcard);
                d.regexp.setCaseSensitivity(options[option].type == IName ? Qt::CaseInsensitive : Qt::CaseSensitive);
                if (!d.regexp.isValid()) {
                    errorMessage = QString("'%1' is not a valid regexp").arg(args.at(i));
                }
                break;
            }

                /*case ::MinDepth:
                  case MaxDepth: {
                  bool ok;
                  const int val = args.at(++i).toUInt(&ok);
                  if (!ok) {
                  errorMessage = QString("'%1' is not a valid unsigned integer").arg(args.at(i));
                  } else {
                  (options[option].type == MinDepth ? minDepth : maxDepth) = val;
                  }

                  break; } */

            case ::Ignore:
            case ::IIgnore: {
                d.ignoreRegexp.setPattern(args.at(++i));
                d.ignoreRegexp.setPatternSyntax(QRegExp::Wildcard);
                d.ignoreRegexp.setCaseSensitivity(options[option].type == Ignore ? Qt::CaseInsensitive : Qt::CaseSensitive);
                if (!d.ignoreRegexp.isValid()) {
                    errorMessage = QString("'%1' is not a valid regexp").arg(args.at(i));
                }
                break;
            }
            case ::Exclude: {
                const QString raw = args.at(++i);
                // /pattern/ selects regex mode; anything else is wildcard.
                // A lone "/" or "//" is treated as a wildcard of that literal.
                if (raw.size() >= 2 && raw.startsWith('/') && raw.endsWith('/')) {
                    d.ignoreRegexp.setPattern(raw.mid(1, raw.size() - 2));
                    d.ignoreRegexp.setPatternSyntax(QRegExp::RegExp);
                } else {
                    d.ignoreRegexp.setPattern(raw);
                    d.ignoreRegexp.setPatternSyntax(QRegExp::Wildcard);
                }
                d.ignoreRegexp.setCaseSensitivity(Qt::CaseSensitive);
                if (!d.ignoreRegexp.isValid()) {
                    errorMessage = QString("'%1' is not a valid pattern").arg(raw);
                }
                break;
            }
            case ::Opacity: {
                const int percentage = args.at(++i).toInt();
                if (percentage <= 0 || percentage > 100) {
                    errorMessage = "percentage must be between 1 and 100";
                } else {
                    setWindowOpacity(double(percentage) / 100.0);
                }
                break;
            }

            case ::QuitTimer:
                if (i + 1 < args.size()) {
                    bool ok;
                    const double tmp = args.at(i + 1).toDouble(&ok);
                    if (ok) {
                        d.quitTimerMinutes = tmp;
                        ++i;
                    }
                }
                break;
            case ::AutoZoom:
                set(AutoZoomEnabled);
                break;
            case ::Recurse:
                status |= RecurseDirs;
                break;
            case ::MaxImageCount:
            case ::MaxThreadCount: {
                bool ok;
                const int tmp = args.at(++i).toUInt(&ok);
                if (tmp < 1) {
                    errorMessage = QString("%1's arg must be a positive integer > 1").arg(arg);
                } else {
                    *(options[option].type == MaxImageCount ? &d.maxImages : &d.maxThreads) = tmp;
                }
                break;
            }
            case ::DashDash:
                status |= SeenDashDash;
                break;
            }
        }
        if (!errorMessage.isEmpty())
            break;
    }
    srand(QTime(0, 0, 0).msecsTo(QTime::currentTime()));

    if (!errorMessage.isEmpty() || status & ShowHelp) {
        QString usage = "Usage: vp2 [options] files/dirs...\n"
            "\n"
            "Options:\n"
            "--------\n";
        int widest = -1;
        for (int i=0; options[i].description; ++i) {
            int width = strlen(options[i].longOpt) + 4;
            switch (options[i].extraArg) {
            case No:
                break;
            case One:
                width += 4;
                break; // ' arg '
            case Two:
                width += 10;
                break; // ' arg1 arg2 '
            case Optional:
                width += 11;
                break; // ' [optional] '
            }
            widest = qMax(widest, width);
        }
        for (int i=0; options[i].description; ++i) {
            QString line;
            line.reserve(100);
            if (options[i].shortOpt) {
                line.append(options[i].shortOpt);
                line.append('|');
            } else {
                line.append("   ");
            }
            line.append(options[i].longOpt);
            switch (options[i].extraArg) {
            case No:
                break;
            case One:
                line.append(" arg");
                break;
            case Two:
                line.append(" arg1 arg2");
                break;
            case Optional:
                line.append(" [optional]");
                break;
            }
            usage.append((line.leftJustified(widest) + options[i].description) + "\n");
        }

        usage.append(errorMessage);

        FILE *stream = errorMessage.isEmpty() ? stdout : stderr;
        fprintf(stream, "%s\n", qPrintable(usage));
        exit(errorMessage.isEmpty() ? 0 : 1);
        return;
    }

    if (pictures.isEmpty() && status & RecurseDirs) {
        Pic pic;
        pic.type = Pic::Dir;
        pic.path = ".";
        pictures.append(pic);
    }

    for (int i=0; i<pictures.size(); ++i) {
        const Pic &pic = pictures.at(i);
        switch (pic.type) {
        case Pic::Dir:
            addDirectory(pic.path, status & RecurseDirs);
            break;
        case Pic::File:
            addFile(pic.path);
            break;
        case Pic::Network:
            if (!d.networkManager) {
                d.networkManager = new QNetworkAccessManager(this);
                connect(d.networkManager, SIGNAL(finished(QNetworkReply*)),
                        this, SLOT(onNetworkReplyFinished(QNetworkReply*)));
            }
            d.networkManager->get(QNetworkRequest(pic.url));
            break;
        }
    }
    if (status & ShowFullScreen) {
        showFullScreen();
    } else {
        show();
    }
    updateImages();
}

void Window::addDirectory(const QString &path, bool recurse)
{
    FileNameThread *thread = new FileNameThread(path, d.regexp, d.ignoreRegexp,
                                                test(DetectFileType), recurse);
    thread->setSizeConstraints(d.minSize, d.maxSize);
    connect(thread, SIGNAL(file(QString)), this, SLOT(addFile(QString)));
    connect(thread, SIGNAL(finished()), this, SLOT(fileNameThreadFinished()));
    d.fileNameThreads.insert(thread);
    thread->start();
}

void Window::wheelEvent(QWheelEvent *e)
{
    switch (e->modifiers()) {
    case Qt::NoModifier:
    case Qt::ShiftModifier:
        if (e->angleDelta().y() < 0) {
            moveCurrentIndexBy(e->modifiers() & Qt::ShiftModifier ? 10 : 1);
        } else {
            moveCurrentIndexBy(e->modifiers() & Qt::ShiftModifier ? -10 : -1);
        }
        break;
    default:
        QAbstractScrollArea::wheelEvent(e);
        break;
    }
    e->accept();
}

void Window::resizeEvent(QResizeEvent *e)
{
    if (!d.updateImagesTimer.isActive())
        d.updateImagesTimer.start(10, this);
    d.updateFontSizeTimer.start(100, this);
    QRect r(0, 0, width(), d.lineEdit->sizeHint().height());
    r.moveBottom(height());
    d.lineEdit->setGeometry(r);
    updateAreas();
#ifdef VIDEO_ENABLED
    if (d.videoDecoder && test(AutoZoomEnabled)) {
        d.videoDecoder->setTargetSize(centerImageTargetSize());
    }
    layoutVideoControls();
#endif
    QAbstractScrollArea::resizeEvent(e);
}

static inline void drawText(QPainter *p, const QRect &eventRect, const QRect &rect, uint alignment,
                            const QFontMetrics &fm, const QString &str)
{
    const QRect textRect = fm.boundingRect(rect, alignment, str);
    if (eventRect.isEmpty() || eventRect.intersects(textRect)) {
        p->drawText(rect, alignment, str);
    }
}

void Window::paintEvent(QPaintEvent *e)
{
    QPainter p(viewport());
    QFont f;
    if (d.fontSize > 0)
        f.setPixelSize(d.fontSize);
    p.setFont(f);
    QFontMetrics fm(f);

    const QRect viewportRect = viewport()->rect();
    QRect eventRect = e->rect();
    if (eventRect == viewport()->rect())
        eventRect = QRect();

    p.setPen(QPen(d.penColor, 2));
    if (test(XKludge))
        p.fillRect(viewportRect, palette().brush(backgroundRole()));

    if (d.data.isEmpty()) {
        if (d.fileNameThreads.isEmpty())
            drawText(&p, eventRect, viewportRect, Qt::AlignCenter, fm, "No images specified");
    } else {
        Data *dt = d.data.at(d.current);
        if (d.toDelete.contains(dt))
            p.fillRect(viewportRect, QColor(255, 0, 0, 75));
        if (dt->flags & Data::Failed) {
            dt->flags |= Data::Seen;
            drawText(&p, eventRect, viewportRect, Qt::AlignCenter, fm, "Can't load " + QFileInfo(dt->path).fileName());
        } else {
            QImage imageToDraw = dt->currentImage();
            if (imageToDraw.isNull()) {
                drawText(&p, eventRect, viewportRect, Qt::AlignCenter, fm, "Loading " + QFileInfo(dt->path).fileName());
            } else {
                const QSize pixmapSize = imageToDraw.size();
                int x, y, sy, sx;
                if (horizontalScrollBar()->isVisible()) {
                    sx = horizontalScrollBar()->value();
                    x = 0;
                } else {
                    sx = 0;
                    x = (viewport()->width() - pixmapSize.width()) / 2;
                }

                if (verticalScrollBar()->isVisible()) {
                    sy = verticalScrollBar()->value();
                    y = 0;
                } else {
                    sy = 0;
                    y = (viewport()->height() - pixmapSize.height()) / 2;
                }

                const QRect source(sx, sy, pixmapSize.width() - sx, pixmapSize.height() - sy);
                const QRect r(QPoint(x, y), pixmapSize);
                if (eventRect.isNull() || eventRect.intersects(r)) {
                    p.drawImage(r, imageToDraw);
                    p.drawRect(r);
                }

                if (!d.rects.isEmpty()) {
                    int idx = 0;
                    QList<Qt::GlobalColor> colors;
                    colors << Qt::black
                           << Qt::white
                           << Qt::darkGray
                           << Qt::gray
                           << Qt::lightGray
                           << Qt::red
                           << Qt::green
                           << Qt::blue
                           << Qt::cyan
                           << Qt::magenta
                           << Qt::yellow
                           << Qt::darkRed
                           << Qt::darkGreen
                           << Qt::darkBlue
                           << Qt::darkCyan
                           << Qt::darkMagenta
                           << Qt::darkYellow;

                    p.save();
                    QFont f;
                    f.setPixelSize(12);
                    p.setFont(f);

                    for (QVector<QRect>::const_iterator it = d.rects.begin(); it != d.rects.end(); ++it) {
                        QRect rr = *it;
                        rr.translate(r.topLeft());
                        QColor color = colors.at(idx++ % colors.size());
                        color.setAlpha(128);

                        p.fillRect(rr, color);
                        if (rr.y() >= 14) {
                            p.drawText(rr.x() + 1, rr.y() - 2, QString::number(it->x()) + "," + QString::number(it->y()) + "+" + QString::number(it->width()) + "x" + QString::number(it->height()));
                        }
                    }
                    p.restore();
                }

                if (test(DisplayThumbnails) && d.data.size() > 1) {
                    int thumbWidth = qMax(d.thumbMinWidth, r.left() - 2);
                    ThumbInfo *thumbs[] = { &d.thumbLeft, &d.thumbRight };
                    // Cap thumb width so its scaled height cannot exceed the
                    // viewport height (prevents top/bottom clipping of tall thumbs).
                    const int vh = viewport()->height();
                    for (int i = 0; i < 2; ++i) {
                        const int idx = bound(d.current + (i == 0 ? -1 : 1));
                        const QSize srcSize = d.data.at(idx)->currentImage().size();
                        if (srcSize.height() > 0 && srcSize.width() > 0) {
                            const int maxWidthForHeight = (vh * srcSize.width()) / srcSize.height();
                            if (maxWidthForHeight > 0 && thumbWidth > maxWidthForHeight) {
                                thumbWidth = maxWidthForHeight;
                            }
                        }
                    }
                    static const bool thumbVerbose = (qgetenv("VP2_THUMB_VERBOSE") == "1");
                    for (int i=0; i<2; ++i) {
                        const int idx = bound(d.current + (i == 0 ? -1 : 1));
                        QImage sourceImage = d.data.at(idx)->currentImage();
                        const bool widthMismatch = thumbWidth != thumbs[i]->image.width();
                        const bool sourceOk = !sourceImage.isNull();
                        const bool threadIdle = !thumbs[i]->thread
                            || thumbs[i]->requestedWidth != thumbWidth;
                        if (thumbVerbose) {
                            qDebug() << "thumb" << (i == 0 ? "L" : "R")
                                     << "idx" << idx
                                     << "path" << d.data.at(idx)->path
                                     << "flags" << d.data.at(idx)->flags
                                     << "srcSize" << sourceImage.size()
                                     << "thumbImgW" << thumbs[i]->image.width()
                                     << "want" << thumbWidth
                                     << "widthMismatch" << widthMismatch
                                     << "sourceOk" << sourceOk
                                     << "threadIdle" << threadIdle
                                     << "thread" << thumbs[i]->thread;
                        }
                        if (widthMismatch && sourceOk && threadIdle) {
                            thumbs[i]->thread = new ThumbLoaderThread(sourceImage, thumbWidth);
                            d.thumbLoaderThreads.insert(thumbs[i]->thread);
                            thumbs[i]->requestedWidth = thumbWidth;
                            connect(thumbs[i]->thread, SIGNAL(finished()),
                                    this, SLOT(onThumbThreadFinished()));
                            connect(thumbs[i]->thread, SIGNAL(thumbLoaded(QImage)),
                                    this, SLOT(onThumbLoaded(QImage)));
                            thumbs[i]->thread->start();
                        }
                    }
                    if (!d.thumbLeft.image.isNull()) {
                        QRect rr = d.thumbLeft.image.rect();
                        rr.moveCenter(r.center());
                        rr.moveLeft(0);
                        p.drawImage(rr, d.thumbLeft.image);
                    }

                    if (!d.thumbRight.image.isNull()) {
                        QRect rr = d.thumbRight.image.rect();
                        rr.moveCenter(r.center());
                        rr.moveRight(viewport()->width());
                        p.drawImage(rr, d.thumbRight.image);
                    }
                }
            }
            if (test(DisplayFileName)) {
                drawText(&p, eventRect, textArea(), Qt::AlignTop|Qt::AlignLeft, fm,
                         dt->path + QString(" %1x%2\n%3 of %4 (%5 images in memory) (%6 in loading queue)").
                         arg(dt->originalSize.width()).
                         arg(dt->originalSize.height()).
                         arg(d.current + 1).
                         arg(d.data.size()).
                         arg(d.imagesInMemory).
                         arg(d.imageLoaderThread.pending()));
            }
        }
    }
    if (d.midButtonPressed) {
        const QRect r(d.pressPosition, QCursor::pos());
        p.drawRect(r);
    }
}

bool Window::rightSize(const QSize &siz, const QSize &widgetSize) const
{
    if (!test(AutoZoomEnabled) || siz == widgetSize)
        return true;
    QSize s = siz;
    s.scale(widgetSize, Qt::KeepAspectRatio);
    return (s == siz);
}

// Returns the target size the center image should be scaled to fit within.
// When thumbnails are displayed (and there is more than one image), reserve
// only a fraction of the thumbnail width on each side so thumbs are allowed
// to overlay the center image by a small margin. This lets the center image
// use most of the viewport while still guaranteeing it is never cropped on
// top/bottom by height-dominant thumbs.
QSize Window::centerImageTargetSize() const
{
    QSize size = viewport()->size();
    if (test(DisplayThumbnails) && d.data.size() > 1) {
        // Reserve ~60% of thumbMinWidth per side; remaining 40% is overlay budget.
        const int reservedPerSide = (d.thumbMinWidth * 3) / 5;
        const int reserved = 2 * reservedPerSide;
        if (size.width() > reserved) {
            size.setWidth(size.width() - reserved);
        }
    }
    return size;
}

void Window::load(int index)
{
    Q_ASSERT(index < d.data.size() && index >= 0);
    Data *dt = d.data.at(index);
    Q_ASSERT(dt);
    if (dt->flags & Data::Failed || d.loading.contains(dt)) {
        // qDebug() << "Not trying to load" << dt->path << "Because" << dt->flags << "but is it really?"
        //          << d.loading.contains(dt);
        return;
    }

    uint flags = 0;
    if (test(NoSmoothScale))
        flags |= ImageLoaderThread::NoSmoothScale;
    if (index == d.current || index == bound(d.current - 1) || index == bound(d.current + 1))
        flags |= ImageLoaderThread::HighPriority;
    QSize size;
    if (test(AutoZoomEnabled)) {
        size = centerImageTargetSize();
        if (dt->movie) {
            QSize currentSize = dt->movie->scaledSize();
            if (currentSize.isNull())
                currentSize = dt->movie->currentImage().size();
            if (!isVisible() || rightSize(currentSize, size))
                return;
            QSize scaledSize = dt->movie->currentImage().size();
            scaledSize.scale(size, Qt::KeepAspectRatio);
            dt->movie->setScaledSize(scaledSize);
            viewport()->update();
            return;
        }
        if (!dt->image.isNull()) {
            if (!isVisible() || rightSize(dt->image.size(), size))
                return;
        }
    } else if (dt->movie) {
        return;
    } else if (!dt->image.isNull()) {
        return;
    }
    d.loading[dt] = index;
#ifdef VIDEO_ENABLED
    if (isVideoPath(dt->path)) {
        // Decode the first frame synchronously on the main thread. For typical
        // video files this is fast (well under a frame budget); we accept the
        // brief hitch to avoid the complexity of per-video loader threads for
        // every neighbor.
        VideoDecoder decoder;
        if (decoder.open(dt->path)) {
            decoder.setTargetSize(size);
            QImage first;
            if (decoder.decodeNextFrame(&first)) {
                dt->flags |= Data::Video;
                if (dt->image.isNull()) {
                    ++d.imagesInMemory;
                }
                dt->image = first;
                dt->originalSize = decoder.frameSize();
            } else {
                dt->flags |= Data::Failed;
            }
        } else {
            dt->flags |= Data::Failed;
        }
        d.loading.remove(dt);
        if (index == d.current) {
            updateAreas();
        }
        viewport()->update();
        return;
    }
#endif
    if (dt->path.endsWith(".pdf", Qt::CaseInsensitive)) {
        dt->flags |= Data::Pdf;
        d.imageLoaderThread.load(0, flags, dt->rotation, dt, size, dt->path);
        return;
    }
    QImageReader *reader = new QImageReader(dt->path);
    if (reader->supportsAnimation()) {
        QMovie *movie = new QMovie(dt->path);
        if (movie->isValid() && movie->frameCount() != 1) {
            dt->movie = movie;
            if (!size.isNull()) {
                QSize scaledSize = reader->size();
                scaledSize.scale(size, Qt::KeepAspectRatio);
                movie->setScaledSize(scaledSize);
            }
            connect(movie, SIGNAL(frameChanged(int)), this, SLOT(onMovieFrameChanged()));
            movie->start();
            d.loading.remove(dt);
            delete reader;
            viewport()->update();
            return;
        }
        delete movie;
    }
    d.imageLoaderThread.load(reader, flags, dt->rotation, dt, size, dt->path);
}

void Window::updateImages()
{
    if (d.data.isEmpty()) {
        return;
    }
    d.updateScrollBarsTimer.start(10, this);
    load(d.current);
    // start this first, it won't start again inside the loop
    if (test(FirstImage))
        return;
    const QSet<int> sur = surrounding(d.current, d.data.size(), d.maxImages);
    foreach(int i, sur) {
        if (i != d.current) {
            load(i);
        }
    }
}


void Window::showEvent(QShowEvent *e)
{
    if (test(HidePointer)) {
        viewport()->setCursor(QCursor(Qt::BlankCursor));
    }

    QTimer::singleShot(0, this, SLOT(updateImages()));
    QAbstractScrollArea::showEvent(e);
    activateWindow();
    raise();
    setFocus();
}

void Window::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == d.quitTimer.timerId()) {
        close();
    } else if (e->timerId() == d.slideShowTimer.timerId()) {
        if (!d.data.isEmpty() && d.loading.contains(d.data.at(d.current)))
            return;
        if (!searchNext()) {
            moveCurrentIndexBy(1);
        }
    } else if (e->timerId() == d.indexBufferTimer.timerId()) {
        d.indexBufferTimer.stop();
        bool ok;
        int i = d.indexBuffer.toUInt(&ok) - 1;
        if (ok && i >= 0 && i < d.data.size())
            setCurrentIndex(i);
    } else if (e->timerId() == d.indexBufferClearTimer.timerId()) {
        d.indexBuffer.clear();
        d.indexBufferClearTimer.stop();
    } else if (e->timerId() == d.updateFontSizeTimer.timerId()) {
        d.updateFontSizeTimer.stop();
        QFont f;
        f.setPixelSize(30);
        const int w = viewport()->width();
        while (QFontMetrics(f).horizontalAdvance(d.longestPath) >= w && f.pixelSize() > 10) {
            f.setPixelSize(f.pixelSize() - 1);
        }
        if (d.fontSize != f.pixelSize()) {
            d.fontSize = f.pixelSize();
            if (d.data.isEmpty() || d.data.at(d.current)->currentImage().isNull()) {
                viewport()->update();
            } else {
                viewport()->update(textArea());
            }
        }
    } else if (e->timerId() == d.updateImagesTimer.timerId()) {
        updateImages();
        d.updateImagesTimer.stop();
    } else if (e->timerId() == d.updateScrollBarsTimer.timerId()) {
        updateScrollBars();
        d.updateScrollBarsTimer.stop();
#ifdef VIDEO_ENABLED
    } else if (e->timerId() == d.videoPlaybackTimer.timerId()) {
        advanceVideoFrame();
#endif
    } else {
        QAbstractScrollArea::timerEvent(e);
    }
}

void Window::addDirectoryRecursively()
{
    const QString dir = QSettings().value("dir", QDir::currentPath()).toString();
    const QString str = QFileDialog::getExistingDirectory(this, "Add directory recursively", dir);
    if (str.isEmpty())
        return;
    QSettings().setValue("dir", str);
    addDirectory(str, true);
}

void Window::addDirectory()
{
    const QString dir = QSettings().value("dir", QDir::currentPath()).toString();
    const QString str = QFileDialog::getExistingDirectory(this, "Add directory", dir);
    if (str.isEmpty())
        return;
    QSettings().setValue("dir", str);
    addDirectory(str, false);
}

void Window::fileNameThreadFinished()
{
    FileNameThread *thread = qobject_cast<FileNameThread*>(sender());
    Q_ASSERT(thread);
    Q_ASSERT(d.fileNameThreads.contains(thread));
    d.fileNameThreads.remove(thread);
    delete thread;
    if (d.fileNameThreads.isEmpty()) {
        updateImages();
    } else if (d.data.isEmpty() && test(DisplayFileName)) {
        viewport()->update(textArea());
    }
#if 0
    for (int i=0; i<data.size(); ++i) {
        qDebug() << i << QFileInfo(d.data.at(i)->path).fileName() << QFileInfo(d.data.at(i)->path).size();
    }
#endif
}

void Window::addImages()
{
    QString dir = QSettings().value("dir", QDir::currentPath()).toString();
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    QString frm = "Images(";
    foreach(const QByteArray &ba, formats) {
        frm += ("*." + ba + " ");
    }
    frm.chop(1);
    frm.append(");; All Files(*)");
    const QStringList list = QFileDialog::getOpenFileNames(this, "Add files", dir, frm);
    if (list.isEmpty())
        return;
    QSettings().setValue("dir", QFileInfo(list.at(0)).absolutePath());
    foreach(const QString &file, list) {
        addFile(file);
    }
    updateImages();
}
void Window::clearImages()
{
    d.imageLoaderThread.clear();
    foreach(Data *dt, d.data) {
        if (!(dt->flags & Data::Network))
            dt->clear();
    }
    d.imagesInMemory = 0;
    updateImages();
}
typedef QList<Data*>::iterator DataIterator;
static inline bool compareDataAlphabetically(const Data *left, const Data *right)
{
    return left->path < right->path;
}

static inline int toUInt(const QStringRef &ref)
{
    int number = 0;
    for (int i=0; i<ref.size(); ++i) {
        if (i > 0) {
            number *= 10;
        }
        number += ref.at(i).toLatin1() - '0';
        Q_ASSERT(ref.at(i).isNumber());
    }
    return number;
}

struct Section {
    Section() : integer(-1) {}
    Section(const QStringRef &r, bool number) : ref(r), integer(-1) {
        if (number)
            integer = ::toUInt(r);
    }

    int compare(const Section &other) const {
        int ret = 0;
        if (integer >= 0 && other.integer >= 0) {
            if (integer < other.integer) {
                ret = -1;
            } else if (integer > other.integer) {
                ret = 1;
            }
        } else {
#if QT_VERSION >= 0x040500
            ret = qBound(-1, ref.compare(other.ref), 1);
#else
            ret = ref < other.ref ? -1 : (ref > other.ref ? 1 : 0);
#endif
        }
        return ret;
    }

    QStringRef ref;
    int integer;
};

static inline QList<Section> encode(const QString *string)
{
    static QHash<const QString*, QList<Section> > data;
    QList<Section> ret;
    if (!data.contains(string)) {
        int last = 0;
        enum { Unset, Number, NotNumber } state = Unset;
        const int size = string->size();
        for (int i=0; i<size; ++i) {
            const bool number = string->at(i).isNumber();
            if (state == Unset) {
                state = (number ? Number : NotNumber);
            } else if (number != (state == Number)) {
                state = (number ? Number : NotNumber);
                const QStringRef ref(string, last, i - last);
                ret.append(Section(ref, !number));
                last = i;
            }
        }
        const QStringRef ref(string, last, size - last);
        ret.append(Section(ref, state == Number));
        data[string] = ret;
    } else {
        ret = data.value(string);
    }
    return ret;

}

static inline bool compareDataNaturally(const Data *left, const Data *right)
{
    const QList<Section> l = encode(&left->path);
    const QList<Section> r = encode(&right->path);
    const int max = qMin(l.size(), r.size());
    for (int i=0; i<max; ++i) {
        switch (l.at(i).compare(r.at(i))) {
        case -1:
            return true;
        case 0:
            break;
        case 1:
            return false;
        }
    }
    return l.size() < r.size();
}


static inline bool compareDataBySize(const Data *left, const Data *right)
{
    static QHash<const Data*, qint64> size;
#define FIND_SIZE(arg)                              \
    qint64 &size_ ## arg = size[arg];               \
    if (size_ ## arg == 0) {                        \
        size_ ## arg = QFileInfo(arg->path).size(); \
    }

    FIND_SIZE(left);
    FIND_SIZE(right);
#undef FIND_SIZE
    return size_left > size_right;
}

static inline bool compareDataByCreationDate(const Data *left, const Data *right)
{
    static QHash<const Data*, uint> date;
#define FIND_DATE(arg)                                              \
    uint &date_ ## arg = date[arg];                                 \
    if (date_ ## arg == 0) {                                        \
        date_ ## arg = QFileInfo(arg->path).birthTime().toTime_t();   \
    }

    FIND_DATE(left);
    FIND_DATE(right);
#undef FIND_DATE
    return date_left > date_right;
}


void Window::addFile(const QString &path)
{
    Data *dt = new Data;
    dt->path = path;
    addNode(dt);
}

void Window::addNode(Data *dt)
{
    if (test(DisplayFileName) && (d.data.size() + 1) % 10 == 0) {
        viewport()->update(textArea());
    }
    if (dt->path.size() > d.longestPath.size()) {
        d.updateFontSizeTimer.start(1000, this);
        d.longestPath = dt->path;
    }
    DataIterator it = d.data.end();
    if (!d.data.isEmpty()) {
        switch (d.sort) {
        case Natural:
            it = std::lower_bound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataNaturally);
            break;
        case Alphabetically:
            it = std::lower_bound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataAlphabetically);
            break;
        case Size:
            it = std::lower_bound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataBySize);
            break;
        case CreationDate:
            it = std::lower_bound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataByCreationDate);
            break;
        case Random:
            it = d.data.begin() + (rand() % d.data.size());
            break;
        case None:
            break;
        }
    }

    if (it == d.data.end()) {
        d.data.append(dt);
    } else {
        const int index = it - d.data.begin();
        d.data.insert(it, dt);
        modifyIndexes(index, 1);
        if (d.current >= index && test(ManuallySetIndex))
            ++d.current;
    }
    if (d.data.size() == 1) {
        setCurrentIndex(0);
    }

    if (d.data.size() <= d.maxImages) {
        updateImages();
    }
}

void Window::toggleSlideShow()
{
    if (d.slideShowTimer.isActive()) {
        d.slideShowTimer.stop();
    } else {
        d.slideShowTimer.start(int(d.slideShowInterval * 1000.0), this);
    }
    updateSpaceShortcutOwner();
}

// Space has three contextual owners depending on what's happening:
//   - active video on center  → toggleVideoPlayback (play/pause)
//   - active slideshow        → toggleSlideShow    (stop)
//   - otherwise               → nextImage          (advance)
// Qt's shortcut system dispatches based on which QAction has the key, so we
// physically move Qt::Key_Space between actions rather than branching inside
// a slot (which wouldn't know which shortcut triggered it).
void Window::updateSpaceShortcutOwner()
{
    if (!d.act.nextImage || !d.act.toggleSlideShow) {
        return;
    }
    QList<QKeySequence> nextShortcuts;
    nextShortcuts << QKeySequence(Qt::Key_Right) << QKeySequence(Qt::Key_Down);
    QList<QKeySequence> slideShortcuts;
    slideShortcuts << QKeySequence(Qt::Key_S);

#ifdef VIDEO_ENABLED
    const bool videoActive = d.videoDecoder != 0;
#else
    const bool videoActive = false;
#endif
    const bool slideActive = d.slideShowTimer.isActive();

    if (videoActive) {
#ifdef VIDEO_ENABLED
        if (d.act.toggleVideoPlayback) {
            QList<QKeySequence> vidShortcuts;
            vidShortcuts << QKeySequence(Qt::Key_P)
                         << QKeySequence(Qt::Key_Return)
                         << QKeySequence(Qt::Key_Enter)
                         << QKeySequence(Qt::Key_Space);
            d.act.toggleVideoPlayback->setShortcuts(vidShortcuts);
        }
#endif
    } else {
#ifdef VIDEO_ENABLED
        if (d.act.toggleVideoPlayback) {
            QList<QKeySequence> vidShortcuts;
            vidShortcuts << QKeySequence(Qt::Key_P)
                         << QKeySequence(Qt::Key_Return)
                         << QKeySequence(Qt::Key_Enter);
            d.act.toggleVideoPlayback->setShortcuts(vidShortcuts);
        }
#endif
        if (slideActive) {
            slideShortcuts << QKeySequence(Qt::Key_Space);
        } else {
            nextShortcuts << QKeySequence(Qt::Key_Space);
        }
    }

    d.act.nextImage->setShortcuts(nextShortcuts);
    d.act.toggleSlideShow->setShortcuts(slideShortcuts);
}

void Window::toggleAutoZoom()
{
    toggle(AutoZoomEnabled);
    updateImages();
#ifdef VIDEO_ENABLED
    if (d.videoDecoder) {
        d.videoDecoder->setTargetSize(test(AutoZoomEnabled)
                                      ? centerImageTargetSize()
                                      : QSize());
    }
#endif
}

void Window::onImageLoadError(void *userData)
{
    Data *dt = reinterpret_cast<Data*>(userData);
    const int idx = d.loading.value(dt, -1);
    d.loading.remove(dt);
    if (idx == -1) {
        return;
    }
    printf("Failed to load %s\n", qPrintable(dt->path));

    if (idx == d.current || test(DisplayFileName)) {
        viewport()->update();
    }

    if (dt->clear())
        --d.imagesInMemory;
    if (test(FirstImage)) {
        unset(FirstImage);
        updateImages();
    }
    if (test(IgnoreFailed)) {
        delete dt;
        if (d.current > idx)
            --d.current;
        modifyIndexes(idx, -1);
    } else {
        dt->flags = Data::Failed;
    }
}

void Window::onImageLoaded(void *userData, const QImage &image, const QSize &originalSize)
{
    static const bool verbose = (qgetenv("VP2_VERBOSE") == "1");
    Data *dt = reinterpret_cast<Data*>(userData);
    d.loading.remove(dt);
    // Re-derive the index from d.data rather than relying on d.loading: after a
    // shuffle d.loading may have been cleared while this load was in flight, and
    // we still want to store the image and refresh the UI if dt is currently
    // visible (center or immediate neighbor).
    const int idx = d.data.indexOf(dt);
    if (verbose) {
        qDebug() << "got image" << idx << "current" << d.current << d.loading.values();
    }

    if (idx == -1)
        return;

    if (dt->image.isNull())
        ++d.imagesInMemory;
    dt->image = image;
    dt->originalSize = originalSize;

    if (idx == d.current) {
        if (!rightSize(image.size(), centerImageTargetSize())) {
            load(d.current);
        }
        d.updateScrollBarsTimer.start(10, this);
        updateAreas();
        viewport()->update();
    } else if (idx == bound(d.current - 1) || idx == bound(d.current + 1)) {
        updateAreas();
        updateThumbnails();
        viewport()->update();
    } else if (test(DisplayFileName)) {
        viewport()->update();
    }

    if (test(FirstImage)) {
        unset(FirstImage);
        updateImages();
    }
}

void Window::onMovieFrameChanged()
{
    if (!d.data.isEmpty()) {
        Data *dt = d.data.at(d.current);
        if (dt->movie) {
            viewport()->update();
        }
    }
}

void Window::debug()
{
    QSet<int> surr = surrounding(d.current, d.data.size(), d.maxImages);
    surr.insert(d.current);

    foreach(int j, surr) {
        qDebug() << j << d.data.at(j)->path
                 << (d.data.at(j)->image.isNull() ? "no image" : "has image")
                 << "status" << d.data.at(j)->flags
                 << (j == d.current ? "<=" : "");
    }
}

int Window::bound(int cnt) const
{
    const int s = d.data.size();
    if (s == 0)
        return -1;
    while (cnt < 0)
        cnt += s;
    while (cnt >= s)
        cnt -= s;
    return cnt;
}

void Window::moveCurrentIndexBy(int count)
{
    setCurrentIndex(bound(d.current + count));
}

void Window::nextImage()
{
    moveCurrentIndexBy(1);
}

void Window::previousImage()
{
    moveCurrentIndexBy(-1);
}

void Window::nextPage()
{
    if (!d.data.isEmpty()) {
        const int count = qMax(1, d.data.size() / 10);
        moveCurrentIndexBy(count);
    }
}

void Window::previousPage()
{
    if (!d.data.isEmpty()) {
        const int count = qMax(1, d.data.size() / 10);
        moveCurrentIndexBy(-count);
    }
}

void Window::home()
{
    setCurrentIndex(0);
}

void Window::end()
{
    setCurrentIndex(d.data.size() - 1);
}

void Window::startSearch()
{
    d.lineEdit->show();
    d.lineEdit->setFocus();
    d.lineEdit->selectAll();
    d.search = true;
}

void Window::startRect()
{
    d.lineEdit->show();
    d.lineEdit->setFocus();
    d.lineEdit->selectAll();
    d.search = false;
    resetLineEditStyleSheet();
}

void Window::toggleCursorVisible()
{
    const bool hide = toggle(HidePointer);
    viewport()->setCursor(QCursor(hide ? Qt::BlankCursor : Qt::ArrowCursor));
    QSettings().setValue("hidePointer", hide);
}

void Window::copyPath() const
{
    if (d.current != -1) {
        QClipboard *clip = qApp->clipboard();
        if (clip->supportsSelection()) {
            clip->setText(d.data.at(d.current)->path, QClipboard::Selection);
        }
        clip->setText(d.data.at(d.current)->path, QClipboard::Clipboard);
    }
}

void Window::showInfo()
{
    QDialog dialog(this, Qt::Drawer);
    QVBoxLayout *l = new QVBoxLayout(&dialog);
    QTreeWidget *tw = new QTreeWidget(&dialog);
    tw->setColumnCount(3);
    tw->setHeaderLabels(QStringList() << "Index" << "Path" << "Thumb");
    for (int i=0; i<d.data.size(); ++i) {
        QTreeWidgetItem *it = new QTreeWidgetItem(tw);
        it->setData(0, Qt::DisplayRole, i);
        it->setData(1, Qt::DisplayRole, d.data.at(i)->path);
        QImage img = d.data.at(i)->currentImage();
        if (!img.isNull())
            it->setData(2, Qt::DecorationRole, img.scaled(40, 40));
        if (i == d.current) {
            it->setSelected(true);
            tw->scrollToItem(it);
        }
    }
    l->addWidget(tw);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close,
                                                 Qt::Horizontal, &dialog);
    l->addWidget(box);
    connect(box, SIGNAL(rejected()), &dialog, SLOT(accept()));
    dialog.exec();
}

void Window::toggleShowThumbnails()
{
    toggle(DisplayThumbnails);
    QSettings().setValue("displayThumbnails", test(DisplayThumbnails));
    updateImages();
    updateAreas();
#ifdef VIDEO_ENABLED
    if (d.videoDecoder && test(AutoZoomEnabled)) {
        d.videoDecoder->setTargetSize(centerImageTargetSize());
    }
#endif
    viewport()->update();
}

void Window::toggleShowFileName()
{
    toggle(DisplayFileName);
    QSettings().setValue("displayFileName", test(DisplayFileName));
    updateAreas();
    viewport()->update();
}

void Window::keyPressEvent(QKeyEvent *e)
{
    restartQuitTimer();
    const int key = e->key();
    if (key >= Qt::Key_0 && key <= Qt::Key_9
        && e->modifiers() == Qt::NoModifier) {
        if (d.data.isEmpty() || e->text().isEmpty()) {
            return;
        }
        d.indexBuffer.append(e->text());
        forever {
            bool ok;
            int i = d.indexBuffer.toInt(&ok) - 1;
            Q_ASSERT(ok);
            if (i >= d.data.size()) {
                d.indexBuffer.remove(0, 1);
            } else {
                d.indexBufferTimer.start(300, this);
                break;
            }
        }
        if (!d.indexBuffer.isEmpty()) {
            d.indexBufferClearTimer.start(2000, this);
        }
        return;
    }
    if (key == Qt::Key_Escape) {
        if (d.indexBuffer.isEmpty()) {
            close();
        } else {
            d.indexBuffer.clear();
            d.indexBufferTimer.stop();
        }
        return;
    }
    d.indexBuffer.clear();
    QAbstractScrollArea::keyPressEvent(e);
}

void Window::setCurrentIndex(int index)
{
    if (index == d.current)
        return;
    d.history.prepend(index);
    enum { Max = 1024 };
    while (d.history.size() > Max)
        d.history.takeLast();

    set(ManuallySetIndex, d.data.size() > 1);
    index = qMax(0, index);
    if (d.data.isEmpty()) {
        d.current = -1;
    } else {
        Q_ASSERT(index < d.data.size());
        QSet<int> surr = surrounding(index, d.data.size(), d.maxImages);
        QSet<int> remove = surrounding(d.current, d.data.size(), d.maxImages);
        if (d.current != index) {
            d.thumbLeft = d.thumbRight = ThumbInfo();
            resetCycleCursors();
            stopCenterVideo();
        }
        d.current = index;
        foreach(int r, remove) {
            if (r != index && !surr.contains(r)) {
                Data *dt = d.data.at(r);
                if (!(dt->flags & Data::Network)) {
                    d.imageLoaderThread.remove(dt);
                    d.loading.remove(dt);
                    if (dt->clear())
                        --d.imagesInMemory;
                }
            }
        }

        updateImages();
        startCenterVideoIfAny();
        viewport()->update();
    }
}

void Window::nextDirectory(int count)
{
    if (d.data.size() < 2)
        return;
    Q_ASSERT(count != 0);
    const int add = (count < 0 ? -1 : 1);
    int max = qAbs(count);
    const QString directory = QFileInfo(d.data.at(d.current)->path).absolutePath();
    int i = d.current;
    while (max != 0) {
        i = bound(i + add);
        if (i == d.current && qAbs(count) == 1) {
            return; // only one dir here
        }
        const QString dir = QFileInfo(d.data.at(i)->path).absolutePath();
        if (dir != directory) {
            --max;
        }
    }
    setCurrentIndex(i);
}

void Window::onThumbLoaded(const QImage &thumb)
{
    if (sender() == d.thumbLeft.thread) {
        d.thumbLeft.image = thumb;
        d.thumbLeft.thread = 0;
        d.thumbLeft.requestedWidth = -1;
    } else if (sender() == d.thumbRight.thread) {
        d.thumbRight.image = thumb;
        d.thumbRight.thread = 0;
        d.thumbRight.requestedWidth = -1;
    } else {
        return;
    }

    updateAreas();
    viewport()->update(); // ### this is a bug. I have to do this or I get painting errors when rotating
    // updateThumbnails();
}

static inline void split(const QRect &rect, QRect *top, QRect *bottom)
{
    Q_ASSERT(top && bottom);
    const int y = rect.center().y();
    *top = rect;
    top->setBottom(y);
    *bottom = rect;
    bottom->setTop(y + 1);
}

static inline void split(const QRect &rect, QRect *top, QRect *middle, QRect *bottom)
{
    Q_ASSERT(top && bottom);
    const int h = rect.height() / 3;
    *top = rect;
    top->setBottom(h);
    *middle = rect;
    middle->setTop(h + 1);
    middle->setBottom(h * 2);
    *bottom = rect;
    bottom->setTop((h * 2) + 1);
}

void Window::updateAreas()
{
    if (d.data.isEmpty() || d.current == -1)
        return;

    const QRect r = viewport()->rect();
    d.areas[Center] = d.data.at(d.current)->currentImage().rect();
    d.areas[Center].moveCenter(r.center());
    const QRect left(0, 0, d.areas[Center].left(), r.height());
    const QRect right(d.areas[Center].right(), 0, left.width(), r.height());
    if (test(DisplayThumbnails) && !d.thumbLeft.image.isNull()) {
        split(left, &d.areas[TopLeft], &d.areas[ThumbLeft], &d.areas[BottomLeft]);
        QRect tr = d.thumbLeft.image.rect();
        tr.moveCenter(d.areas[ThumbLeft].center());
        d.areas[ThumbLeft] = tr;
    } else {
        d.areas[ThumbLeft] = QRect();
        split(left, &d.areas[TopLeft], &d.areas[BottomLeft]);
    }

    if (test(DisplayThumbnails) && !d.thumbRight.image.isNull()) {
        split(right, &d.areas[TopRight], &d.areas[ThumbRight], &d.areas[BottomRight]);
        QRect tr = d.thumbRight.image.rect();
        tr.moveCenter(d.areas[ThumbRight].center());
        d.areas[ThumbRight] = tr;
    } else {
        d.areas[ThumbRight] = QRect();
        split(right, &d.areas[TopRight], &d.areas[BottomRight]);
    }
    d.areas[BottomRight].adjust(0, 0, 0, 1);
    d.areas[BottomLeft].adjust(0, 0, 0, 1); // ### hacks
    d.areas[Top].setRect(0, 0, r.width(), d.areas[Center].top());
    const int y = qMin(d.areas[Center].bottom(), r.height() - 20);
    d.areas[Bottom].setRect(0, y, r.width(), r.height() - y);
#if 0
    static const char *str[] = {
        "Top",
        "Bottom",
        "TopLeft",
        "ThumbLeft",
        "BottomLeft",
        "Center",
        "TopRight",
        "ThumbRight",
        "BottomRight",
        0
    };
    qDebug() << rect() << viewport()->rect();
    qDebug() << "------------";
    for (int i=0; str[i]; ++i) {
        qDebug() << str[i] << d.areas[i];
    }
#endif
}

void Window::onThumbThreadFinished()
{
    ThumbLoaderThread *t = qobject_cast<ThumbLoaderThread*>(sender());
    Q_ASSERT(t);
    Q_ASSERT(d.thumbLoaderThreads.contains(t));
    d.thumbLoaderThreads.remove(t);
    delete t;
}

void Window::closeEvent(QCloseEvent *e)
{
    QSettings().setValue("dir", QVariant());
    set(Closing);
    if (purge()) {
        QAbstractScrollArea::closeEvent(e);
    } else {
        e->ignore();
        unset(Closing);
    }
}

bool Window::purge()
{
    if (d.toDelete.isEmpty())
        return true;
    QStringList list;
    foreach(const Data *dt, d.toDelete) {
        list.append(dt->path);
    }
    // ### todo, nicer dialog with thumbnails of images

    switch (QMessageBox::question(this, tr("Delete images"), tr("These images are marked for deletion:\n")
                                  + list.join("\n") + "\n" + tr("Are you sure?"),
                                  tr("Yes"), test(Closing) ? tr("No, but close ") + QCoreApplication::applicationName()
                                  : QString(),
                                  test(Closing) ? tr("Abort") : tr("No"))) {
    case 0:
        foreach(Data *dt, d.toDelete) {
            removeFile(dt);
        }
        if (!test(Closing))
            d.toDelete.clear();
        return true;
    case 1:
        return true;
    case 2:
        return false;
    default:
        break;
    }
    Q_ASSERT(0);
    return true;
}
void Window::removeFile(Data *dt)
{
    QFile file(dt->path);
    const QString fn = QFileInfo(file).fileName();
    file.copy(backupDir().absolutePath() + "/" + fn);
    file.remove();
    if (!test(Closing)) {
        const int index = d.data.indexOf(dt);
        if (index != -1) {
            d.data.removeAt(index);
            if (d.current >= index)
                --d.current;
            if (d.data.isEmpty()) {
                d.current = -1;
            } else {
                d.current = qBound(0, d.current, d.data.size() - 1);
            }
            delete dt;
        }
        if (!d.updateImagesTimer.isActive())
            d.updateImagesTimer.start(0, this);
    }
}
void Window::toggleRemoveCurrentImage()
{
    if (d.data.isEmpty() || d.current == -1)
        return;
    Data *dt = d.data.at(d.current);
    if (dt->flags & Data::Network)
        return;
    if (d.toDelete.contains(dt)) {
        d.toDelete.remove(dt);
    } else {
        d.toDelete.insert(dt);
    }
    viewport()->update();
}

void Window::undeleteCurrentImage()
{
    if (d.data.isEmpty() || d.current == -1)
        return;
    Data *dt = d.data.at(d.current);
    if (d.toDelete.contains(dt)) {
        d.toDelete.remove(dt);
        viewport()->update();
    }
}

void Window::removeCurrentImage()
{
    if (d.data.isEmpty() || d.current == -1)
        return;
    Data *dt = d.data.at(d.current);
    if (dt->flags & Data::Network)
        return;

    if (!d.toDelete.contains(dt)) {
        d.toDelete.insert(dt);
        viewport()->update();
    }
}
void Window::updateScrollBars()
{
    const QSize vs = viewport()->size();
    const QSize s = d.current == -1 ? QSize() : d.data.at(d.current)->currentImage().size();
    const int scrollBarSize = horizontalScrollBar()->sizeHint().height();
    const bool needh = !test(AutoZoomEnabled) && s.height() > vs.height();
    const bool needw = !test(AutoZoomEnabled) && s.width() > vs.width();
    const bool mightneedh = s.height() + scrollBarSize > vs.height();
    const bool mightneedw = s.width() + scrollBarSize > vs.width();
    if (needh || (needw && mightneedh)) {
        verticalScrollBar()->setRange(0, s.height() - vs.height() - scrollBarSize);
    } else {
        verticalScrollBar()->setRange(0, 0);
    }

    if (needw || (needh && mightneedw)) {
        horizontalScrollBar()->setRange(0, s.width() - vs.width() - scrollBarSize);
    } else {
        horizontalScrollBar()->setRange(0, 0);
    }

}
void Window::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    viewport()->update();
    //    viewport()->scroll(-dx, -dy);
}


void Window::restartQuitTimer()
{
    if (d.quitTimerMinutes > 0) {
        d.quitTimer.start(int(d.quitTimerMinutes * 60 * 1000), this);
    }
}

int Window::indexOf(const QString &string, int index) const
{
    const int max = d.data.size();
    while (index < max) {
        if (d.data.at(index)->path.contains(string, Qt::CaseInsensitive))
            return index;
        ++index;
    }
    return -1;
}

int Window::lastIndexOf(const QString &string, int index) const
{
    while (index >= 0) {
        if (d.data.at(index)->path.contains(string, Qt::CaseInsensitive))
            return index;
        --index;
    }
    return -1;
}

bool Window::searchNext()
{
    if (!d.search)
        return false;
    const QString text = d.lineEdit->text();
    if (text.isEmpty())
        return false;
    int i = indexOf(text, d.current + 1);
    if (i == -1) {
        i = indexOf(text, 0);
    }
    if (i != -1) {
        setCurrentIndex(i);
    }
    return true;
}

bool Window::searchPrevious()
{
    if (!d.search)
        return false;
    const QString text = d.lineEdit->text();
    if (text.isEmpty())
        return false;
    int i = lastIndexOf(text, d.current - 1);
    if (i == -1) {
        i = lastIndexOf(text, d.data.size() - 1);
    }
    if (i != -1) {
        setCurrentIndex(i);
    }
    return true;
}

void Window::onLineEditReturnPressed()
{
    if (d.search) {
        const int old = d.current;
        searchNext();
        if (old != d.current) {
            d.lineEdit->hide();
        } else {
            d.lineEdit->setStyleSheet("background: red");
            QTimer::singleShot(1000, this, SLOT(resetLineEditStyleSheet()));
        }
    } else {
        const QStringList splits = d.lineEdit->text().split(";", Qt::SkipEmptyParts);
        bool ok = false;
        for (int i=0; i<splits.size(); ++i) {
            QRegExp rx(" *([0-9]+)[^0-9]+([0-9]+)[^0-9]+([0-9]+)[^0-9]+([0-9]+) *");
            if (!rx.exactMatch(splits.at(i))) {
                qDebug() << "Can't parse rect" << d.lineEdit->text();
            } else {
                ok = true;
                QRect r(rx.cap(1).toInt(), rx.cap(2).toInt(), rx.cap(3).toInt(), rx.cap(4).toInt());
                d.rects.append(r);
            }
        }
        if (ok) {
            viewport()->update();
            d.lineEdit->hide();
        }
    }
}

void Window::resetLineEditStyleSheet()
{
    d.lineEdit->setStyleSheet(QString());
}

void Window::about()
{
    QDialog dlg(this);
    QVBoxLayout *l = new QVBoxLayout(&dlg);
    QString string;
    foreach(const QByteArray &ba, QImageReader::supportedImageFormats()) {
        string += ba + "\n";
    }

    if (d.current != -1) {
        QImage img = d.data.at(d.current)->currentImage();
        if (!img.isNull())
            string += QString("%1 x %2\n").arg(img.width()).arg(img.height());
    }

    QLabel *lbl = new QLabel(string, &dlg);
    l->addWidget(lbl);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, &dlg);
    connect(box, SIGNAL(accepted()), &dlg, SLOT(accept()));
    l->addWidget(box);
    dlg.exec();
}

QRect Window::textArea() const
{
    QFont f;
    if (d.fontSize > 0)
        f.setPixelSize(d.fontSize);
    enum { Margin = 2 };
    const int h = QFontMetrics(f).height();
    return QRect(0, Margin, viewport()->width(), h * 3).adjusted(-1, -1, 1, 1);
}

void Window::ensurePointerHidden()
{
    viewport()->setCursor(QCursor(test(HidePointer) ? Qt::BlankCursor : Qt::ArrowCursor));
}

template <class T>
inline QDebug operator<<(QDebug debug, const std::list<T> &list)
{
    debug.nospace() << "(";
    bool first = true;
    for (typename std::list<T>::const_iterator i = list.begin(); i != list.end(); ++i) {
        if (!first) {
            debug << ", ";
        } else {
            first = false;
        }
        debug << *i;
    }
    debug << ")";
    return debug.space();
}

void Window::back()
{
    if (!d.history.isEmpty()) {
        //        qDebug() << d.history;
        d.history.append(d.history.takeFirst());
        set(InNextPrev);
        setCurrentIndex(d.history.first());
        d.history.takeFirst();
        unset(InNextPrev);
    }
}

void Window::forward()
{
    if (!d.history.isEmpty()) {
        //        qDebug() << d.history;
        d.history.prepend(d.history.takeLast());
        set(InNextPrev);
        setCurrentIndex(d.history.first());
        d.history.takeFirst();
        unset(InNextPrev);
    }
}

void Window::onNetworkReplyFinished(QNetworkReply *reply)
{
    Data *node = new Data;
    node->flags = Data::Network;

    const QByteArray data = reply->readAll();
    if (!data.isEmpty()) {
        QBuffer buffer;
        buffer.setData(data);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        QSize s = reader.size();
        // See threads.cpp: QImageReader::size() is pre-transform. Transpose so
        // aspect-fit targets the orientation the user will actually see.
        const bool exifSwapsAxes = reader.transformation() & QImageIOHandler::TransformationRotate90;
        if (exifSwapsAxes) {
            s.transpose();
        }
        if (test(AutoZoomEnabled)) {
            const QSize target = centerImageTargetSize();
            if (s != target) {
                s.scale(target, Qt::KeepAspectRatio);
                QSize readerScaled = s;
                if (exifSwapsAxes) {
                    readerScaled.transpose();
                }
                reader.setScaledSize(readerScaled);
            }
        }
        node->originalSize = s;
        node->image = reader.read();
    }
    node->path = reply->url().toString();
    if (node->image.isNull()) {
        node->flags |= Data::Failed;
    } else {
        ++d.imagesInMemory;
    }
    addNode(node);
    reply->deleteLater();
}

void Window::shuffle()
{
    if (d.data.isEmpty())
        return;
    Data *current = d.data.at(d.current);
    // Drop any queued image loads so their results (with now-stale indices)
    // don't race updateImages() below. Leaves loaded dt->image data intact.
    d.imageLoaderThread.clear();
    d.loading.clear();
    d.history.clear();
    d.sort = Random;
    for (int i = d.data.size() - 1; i > 0; --i) {
        const int j = rand() % (i + 1);
        std::swap(d.data[i], d.data[j]);
    }
    d.current = d.data.indexOf(current);
    d.thumbLeft = d.thumbRight = ThumbInfo();
    updateImages();
    viewport()->update();
}

static void shuffleNeighborSlot(QList<Data*> &data, int current, int offset)
{
    const int size = data.size();
    if (size < 4) {
        return;
    }
    int slot = current + offset;
    while (slot < 0) {
        slot += size;
    }
    while (slot >= size) {
        slot -= size;
    }
    if (slot == current) {
        return;
    }
    const int other = (offset > 0) ? current - 1 : current + 1;
    int normalizedOther = other;
    while (normalizedOther < 0) {
        normalizedOther += size;
    }
    while (normalizedOther >= size) {
        normalizedOther -= size;
    }
    int pick;
    do {
        pick = rand() % size;
    } while (pick == current || pick == slot || pick == normalizedOther);
    std::swap(data[slot], data[pick]);
}

void Window::shufflePrev()
{
    if (d.data.size() < 4 || d.current == -1) {
        return;
    }
    shuffleNeighborSlot(d.data, d.current, -1);
    d.thumbLeft = ThumbInfo();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::shuffleNext()
{
    if (d.data.size() < 4 || d.current == -1) {
        return;
    }
    shuffleNeighborSlot(d.data, d.current, 1);
    d.thumbRight = ThumbInfo();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::shuffleCenter()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    const int left = (d.current - 1 + size) % size;
    const int right = (d.current + 1) % size;
    int pick;
    do {
        pick = rand() % size;
    } while (pick == d.current || pick == left || pick == right);
    std::swap(d.data[d.current], d.data[pick]);
    // Swapping Data* keeps d.current pointing at the same slot index, which
    // now holds a different image. Thumbs (left/right neighbors) are unchanged.
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::resetCycleCursors()
{
    d.cycleCursor[0] = -1;
    d.cycleCursor[1] = 0;
    d.cycleCursor[2] = 1;
}

namespace {
// Advance a cycle cursor by `direction` (±1), skipping the three slot offsets
// that are currently visible (-1, 0, +1 relative to current).
void advanceCycleCursor(int &cursor, int direction, int size)
{
    if (size <= 3) {
        return;
    }
    cursor += direction;
    while (cursor == -1 || cursor == 0 || cursor == 1) {
        cursor += direction;
    }
    // Keep cursor in a bounded range so repeated cycling doesn't overflow.
    // The modulo maps to a unique image slot relative to current.
    while (cursor < -size) {
        cursor += size;
    }
    while (cursor > size) {
        cursor -= size;
    }
}
}

void Window::cyclePrevForward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[0], 1, size);
    const int slotIdx = (d.current - 1 + size) % size;
    const int targetIdx = ((d.current + d.cycleCursor[0]) % size + size) % size;
    if (slotIdx == targetIdx) {
        return;
    }
    std::swap(d.data[slotIdx], d.data[targetIdx]);
    d.thumbLeft = ThumbInfo();
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::cyclePrevBackward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[0], -1, size);
    const int slotIdx = (d.current - 1 + size) % size;
    const int targetIdx = ((d.current + d.cycleCursor[0]) % size + size) % size;
    if (slotIdx == targetIdx) {
        return;
    }
    std::swap(d.data[slotIdx], d.data[targetIdx]);
    d.thumbLeft = ThumbInfo();
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::cycleCenterForward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[1], 1, size);
    const int targetIdx = ((d.current + d.cycleCursor[1]) % size + size) % size;
    if (targetIdx == d.current) {
        return;
    }
    std::swap(d.data[d.current], d.data[targetIdx]);
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::cycleCenterBackward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[1], -1, size);
    const int targetIdx = ((d.current + d.cycleCursor[1]) % size + size) % size;
    if (targetIdx == d.current) {
        return;
    }
    std::swap(d.data[d.current], d.data[targetIdx]);
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::cycleNextForward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[2], 1, size);
    const int slotIdx = (d.current + 1) % size;
    const int targetIdx = ((d.current + d.cycleCursor[2]) % size + size) % size;
    if (slotIdx == targetIdx) {
        return;
    }
    std::swap(d.data[slotIdx], d.data[targetIdx]);
    d.thumbRight = ThumbInfo();
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

void Window::cycleNextBackward()
{
    const int size = d.data.size();
    if (size < 4 || d.current == -1) {
        return;
    }
    advanceCycleCursor(d.cycleCursor[2], -1, size);
    const int slotIdx = (d.current + 1) % size;
    const int targetIdx = ((d.current + d.cycleCursor[2]) % size + size) % size;
    if (slotIdx == targetIdx) {
        return;
    }
    std::swap(d.data[slotIdx], d.data[targetIdx]);
    d.thumbRight = ThumbInfo();
    d.imageLoaderThread.clear();
    d.loading.clear();
    updateImages();
    updateAreas();
    viewport()->update();
}

#ifdef VIDEO_ENABLED
void Window::startCenterVideoIfAny()
{
    stopCenterVideo();
    if (d.current == -1) {
        return;
    }
    Data *dt = d.data.at(d.current);
    if (!(dt->flags & Data::Video)) {
        return;
    }
    d.videoDecoder = new VideoDecoder;
    if (!d.videoDecoder->open(dt->path)) {
        delete d.videoDecoder;
        d.videoDecoder = 0;
        return;
    }
    if (test(AutoZoomEnabled)) {
        d.videoDecoder->setTargetSize(centerImageTargetSize());
    }
    d.videoDecoderOwner = dt;
    d.videoPaused = false;
    const double fps = d.videoDecoder->frameRate();
    const int intervalMs = fps > 0.0 ? qMax(1, int(1000.0 / fps)) : 40;
    d.videoPlaybackTimer.start(intervalMs, this);
    updateVideoControlsVisibility();
    updateSpaceShortcutOwner();
}

void Window::stopCenterVideo()
{
    d.videoPlaybackTimer.stop();
    if (d.videoDecoder) {
        delete d.videoDecoder;
        d.videoDecoder = 0;
    }
    d.videoDecoderOwner = 0;
    d.videoPaused = false;
    updateVideoControlsVisibility();
    updateSpaceShortcutOwner();
}

void Window::advanceVideoFrame()
{
    if (!d.videoDecoder || !d.videoDecoderOwner) {
        return;
    }
    QImage frame;
    if (!d.videoDecoder->decodeNextFrame(&frame)) {
        // End of stream; rewind and continue looping.
        if (!d.videoDecoder->seek(0.0)) {
            stopCenterVideo();
            return;
        }
        if (!d.videoDecoder->decodeNextFrame(&frame)) {
            stopCenterVideo();
            return;
        }
    }
    d.videoDecoderOwner->image = frame;
    updatePositionSlider();
    viewport()->update();
}

void Window::toggleVideoPlayback()
{
    if (!d.videoDecoder) {
        return;
    }
    d.videoPaused = !d.videoPaused;
    if (d.videoPaused) {
        d.videoPlaybackTimer.stop();
    } else {
        const double fps = d.videoDecoder->frameRate();
        const int intervalMs = fps > 0.0 ? qMax(1, int(1000.0 / fps)) : 40;
        d.videoPlaybackTimer.start(intervalMs, this);
    }
    updateVideoControlsVisibility();
}

void Window::videoSeekForward()
{
    if (!d.videoDecoder) {
        return;
    }
    const double duration = d.videoDecoder->durationSeconds();
    double target = d.videoDecoder->currentSeconds() + d.videoSeekSeconds;
    if (duration > 0.0 && target > duration - 0.1) {
        target = qMax(0.0, duration - 0.1);
    }
    if (d.videoDecoder->seek(target)) {
        advanceVideoFrame();
    }
}

void Window::videoSeekBackward()
{
    if (!d.videoDecoder) {
        return;
    }
    double target = d.videoDecoder->currentSeconds() - d.videoSeekSeconds;
    if (target < 0.0) {
        target = 0.0;
    }
    if (d.videoDecoder->seek(target)) {
        advanceVideoFrame();
    }
}

namespace {
// Cap the skip step at 10% of total duration so short videos don't fast-forward
// past the end (or back to zero) in a single keypress. Falls back to 10s for
// unknown-duration streams.
double cappedSkipStep(double durationSeconds)
{
    double step = 10.0;
    if (durationSeconds > 0.0) {
        step = qMin(step, durationSeconds * 0.1);
    }
    return step;
}
}

void Window::videoSkipForward10()
{
    if (!d.videoDecoder) {
        return;
    }
    const double duration = d.videoDecoder->durationSeconds();
    const double step = cappedSkipStep(duration);
    double target = d.videoDecoder->currentSeconds() + step;
    if (duration > 0.0 && target > duration - 0.1) {
        target = qMax(0.0, duration - 0.1);
    }
    if (d.videoDecoder->seek(target)) {
        advanceVideoFrame();
        updatePositionSlider();
    }
}

void Window::videoSkipBackward10()
{
    if (!d.videoDecoder) {
        return;
    }
    const double step = cappedSkipStep(d.videoDecoder->durationSeconds());
    double target = d.videoDecoder->currentSeconds() - step;
    if (target < 0.0) {
        target = 0.0;
    }
    if (d.videoDecoder->seek(target)) {
        advanceVideoFrame();
        updatePositionSlider();
    }
}

void Window::onPositionSliderPressed()
{
    d.positionSliderPressed = true;
}

void Window::onPositionSliderReleased()
{
    d.positionSliderPressed = false;
    if (!d.videoDecoder || !d.positionSlider) {
        return;
    }
    const double duration = d.videoDecoder->durationSeconds();
    if (duration <= 0.0) {
        return;
    }
    const double frac = double(d.positionSlider->value())
        / double(d.positionSlider->maximum());
    if (d.videoDecoder->seek(frac * duration)) {
        advanceVideoFrame();
    }
}

void Window::onPositionSliderMoved(int value)
{
    if (!d.videoDecoder) {
        return;
    }
    const double duration = d.videoDecoder->durationSeconds();
    if (duration <= 0.0 || !d.positionSlider) {
        return;
    }
    const double frac = double(value) / double(d.positionSlider->maximum());
    if (d.videoDecoder->seek(frac * duration)) {
        advanceVideoFrame();
    }
}

void Window::createVideoControls()
{
    d.videoControls = new QWidget(this);
    d.videoControls->setAttribute(Qt::WA_NoChildEventsForParent, false);
    d.videoControls->setFocusPolicy(Qt::NoFocus);
    d.videoControls->setAutoFillBackground(true);
    QPalette pal = d.videoControls->palette();
    pal.setColor(QPalette::Window, QColor(0, 0, 0, 160));
    pal.setColor(QPalette::WindowText, Qt::white);
    d.videoControls->setPalette(pal);

    QHBoxLayout *l = new QHBoxLayout(d.videoControls);
    l->setContentsMargins(8, 4, 8, 4);
    l->setSpacing(6);

    d.skipBackButton = new QToolButton(d.videoControls);
    d.skipBackButton->setFocusPolicy(Qt::NoFocus);
    d.skipBackButton->setText(QString::fromUtf8("\u23EA"));
    d.skipBackButton->setToolTip(tr("Skip backward (up to 10s, capped at 10% of duration)"));
    connect(d.skipBackButton, SIGNAL(clicked()), this, SLOT(videoSkipBackward10()));
    l->addWidget(d.skipBackButton);

    d.playPauseButton = new QToolButton(d.videoControls);
    d.playPauseButton->setFocusPolicy(Qt::NoFocus);
    d.playPauseButton->setText(QString::fromUtf8("\u23F8"));
    d.playPauseButton->setToolTip(tr("Play/pause"));
    connect(d.playPauseButton, SIGNAL(clicked()), this, SLOT(toggleVideoPlayback()));
    l->addWidget(d.playPauseButton);

    d.skipForwardButton = new QToolButton(d.videoControls);
    d.skipForwardButton->setFocusPolicy(Qt::NoFocus);
    d.skipForwardButton->setText(QString::fromUtf8("\u23E9"));
    d.skipForwardButton->setToolTip(tr("Skip forward (up to 10s, capped at 10% of duration)"));
    connect(d.skipForwardButton, SIGNAL(clicked()), this, SLOT(videoSkipForward10()));
    l->addWidget(d.skipForwardButton);

    d.positionSlider = new QSlider(Qt::Horizontal, d.videoControls);
    d.positionSlider->setFocusPolicy(Qt::NoFocus);
    d.positionSlider->setRange(0, 1000);
    d.positionSlider->setSingleStep(5);
    d.positionSlider->setPageStep(50);
    connect(d.positionSlider, SIGNAL(sliderPressed()),
            this, SLOT(onPositionSliderPressed()));
    connect(d.positionSlider, SIGNAL(sliderReleased()),
            this, SLOT(onPositionSliderReleased()));
    connect(d.positionSlider, SIGNAL(sliderMoved(int)),
            this, SLOT(onPositionSliderMoved(int)));
    l->addWidget(d.positionSlider, 1);

    d.videoControls->hide();
}

void Window::layoutVideoControls()
{
    if (!d.videoControls) {
        return;
    }
    const int ctlH = d.videoControls->sizeHint().height();
    QRect r(0, 0, width(), ctlH);
    r.moveBottom(height());
    d.videoControls->setGeometry(r);
    d.videoControls->raise();
}

void Window::updateVideoControlsVisibility()
{
    if (!d.videoControls) {
        return;
    }
    const bool show = d.videoDecoder != 0;
    if (show != d.videoControls->isVisible()) {
        d.videoControls->setVisible(show);
    }
    if (show) {
        layoutVideoControls();
        d.playPauseButton->setText(d.videoPaused
            ? QString::fromUtf8("\u25B6")
            : QString::fromUtf8("\u23F8"));
    }
}

void Window::updatePositionSlider()
{
    if (!d.positionSlider || !d.videoDecoder || d.positionSliderPressed) {
        return;
    }
    const double duration = d.videoDecoder->durationSeconds();
    if (duration <= 0.0) {
        return;
    }
    const double frac = d.videoDecoder->currentSeconds() / duration;
    const int val = qBound(0, int(frac * d.positionSlider->maximum()),
                           d.positionSlider->maximum());
    if (val != d.positionSlider->value()) {
        d.positionSlider->blockSignals(true);
        d.positionSlider->setValue(val);
        d.positionSlider->blockSignals(false);
    }
}
#else
void Window::startCenterVideoIfAny() {}
void Window::stopCenterVideo() {}
void Window::advanceVideoFrame() {}
void Window::toggleVideoPlayback() {}
void Window::videoSeekForward() {}
void Window::videoSkipForward10() {}
void Window::videoSkipBackward10() {}
void Window::onPositionSliderMoved(int) {}
void Window::onPositionSliderPressed() {}
void Window::onPositionSliderReleased() {}
void Window::createVideoControls() {}
void Window::layoutVideoControls() {}
void Window::updateVideoControlsVisibility() {}
void Window::updatePositionSlider() {}
void Window::videoSeekBackward() {}
#endif

void Window::randomImage()
{
    if (d.data.size() <= 1) {
        return;
    }
    const int index = rand() % d.data.size();
    setCurrentIndex(index);
}

void Window::randomSearchNext()
{
    if (d.data.size() <= 1 || !d.search || d.lineEdit->text().isEmpty()) {
        return;
    }
    int count = rand() % (d.data.size() / 10 + 1);
    while (count--) {
        searchNext();
    }
}

void Window::cyclePenColor()
{
    static const Qt::GlobalColor colors[] = {
        Qt::white, Qt::black, Qt::yellow, Qt::green, Qt::cyan, Qt::transparent
    };
    static int idx = 0;
    ++idx;
    if (colors[idx] == Qt::transparent) {
        idx = 0;
    }
    d.penColor = colors[idx];
    viewport()->update();
}

void Window::slideshowFaster()
{
    d.slideShowInterval *= 0.9;
    d.slideShowTimer.start(int(d.slideShowInterval * 1000.0), this);
}

void Window::slideshowSlower()
{
    d.slideShowInterval *= 1.1;
    d.slideShowTimer.start(int(d.slideShowInterval * 1000.0), this);
}

void Window::printPath()
{
    if (d.current != -1) {
        printf("%s\n", qPrintable(d.data.at(d.current)->path));
        fflush(stdout);
    }
}

void Window::toggleFullScreen()
{
    if (windowState() & Qt::WindowFullScreen) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void Window::showMaximizedSlot()
{
    showMaximized();
}

void Window::showNormalSlot()
{
    showNormal();
}

void Window::quitSlot()
{
    close();
}

void Window::rotateLeft()
{
#ifdef VIDEO_ENABLED
    // When a video is the center image, the rotate shortcut instead seeks
    // backward 10s. Rotating a video mid-playback isn't meaningful and we
    // want [ / ] to feel like scrub keys while watching.
    if (d.videoDecoder) {
        videoSkipBackward10();
        return;
    }
#endif
    if (d.current != -1) {
        Data *data = d.data.at(d.current);
        if (data->rotation == 0) {
            data->rotation = 270;
        } else {
            data->rotation -= 90;
        }
        if (!data->image.isNull()) {
            QTransform transform;
            transform.rotate(-90);
            data->image = data->image.transformed(transform);
            updateAreas();
            viewport()->update();
        }
    }
}

void Window::rotateRight()
{
#ifdef VIDEO_ENABLED
    if (d.videoDecoder) {
        videoSkipForward10();
        return;
    }
#endif
    if (d.current != -1) {
        Data *data = d.data.at(d.current);
        if (data->rotation == 270) {
            data->rotation = 0;
        } else {
            data->rotation += 90;
        }
        if (!data->image.isNull()) {
            QTransform transform;
            transform.rotate(90);
            data->image = data->image.transformed(transform);
            updateAreas();
            viewport()->update();
        }
    }
}
void Window::modifyIndexes(int index, int added)
{
    for (QHash<Data*, int>::iterator it = d.loading.begin(); it != d.loading.end(); ++it) {
        if (it.value() >= index) {
            it.value() += added;
        }
    }
    for (auto i = d.history.begin(); i != d.history.end(); ++i) {
        if (*i >= index)
            (*i) += added;
    }
}
