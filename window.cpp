#include "window.h"
#ifdef MAGICK_ENABLED
#include <Magick++/Image.h>
#include <Magick++/Geometry.h>
#endif

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

    //    setViewport(new Viewport(this));
    d.lineEdit = new QLineEdit(this);
    new QShortcut(QKeySequence(Qt::Key_Escape), d.lineEdit, SLOT(hide()));
    //     new QShortcut(QKeySequence(Qt::Key_F6), this, SLOT(debug()));

    connect(d.lineEdit, SIGNAL(returnPressed()), this, SLOT(onLineEditReturnPressed()));
    d.lineEdit->hide();

    setMouseTracking(true);
    d.longestPath = QLatin1String("No Images Specified");
    set(DisplayFileName, QSettings().value("displayFileName", false).toBool());
    set(DisplayThumbnails, QSettings().value("displayThumbnails", false).toBool());
    set(HidePointer, QSettings().value("hidePointer", false).toBool());
    set(AutoZoomEnabled, QSettings().value("autoZoom", true).toBool());

    setBackgroundColor(QSettings().value("bgcol", "grid").toString().toLower());
    parseArgs(args);

    const QList<QFileInfo> files = backupDir().entryInfoList(QDir::Files|QDir::NoDotAndDotDot);
    if (!files.isEmpty()) {
        const QDateTime current = QDateTime::currentDateTime();
        foreach(const QFileInfo &fi, files) {
            if (current.secsTo(fi.created()) >= 3600 * 24) {
                QFile::remove(fi.absoluteFilePath());
                qDebug() << "Actually removing" << fi.absoluteFilePath();
            }
        }
    }
    connect(&d.imageLoaderThread, SIGNAL(imageLoaded(void*, QImage)),
            this, SLOT(onImageLoaded(void *, QImage)));
    connect(&d.imageLoaderThread, SIGNAL(loadError(void*)),
            this, SLOT(onImageLoadError(void *)));
    d.imageLoaderThread.start();
}

Window::~Window()
{
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
        if (e->button() == Qt::MidButton) {
            d.midButtonPressed = true;
            d.pressPosition = e->pos();
            viewport()->update();
        }
        QAbstractScrollArea::mousePressEvent(e);
    }
}

void Window::mouseReleaseEvent(QMouseEvent *e)
{
    if (d.midButtonPressed) {
        d.midButtonPressed = false;
        viewport()->update();
    }
}

void Window::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    const QAction *addFiles = menu.addAction(tr("&Add files"));
    const QAction *addDirs = menu.addAction(tr("Add &directory"));
    const QAction *addDirsRecursively = menu.addAction(tr("Add directory (&recursively)"));
    const QAction *displayFileNameAct = menu.addAction(test(DisplayFileName)
                                                       ? tr("Hide &file name")
                                                       : tr("Display &file name"));
    const QAction *displayThumbnails = menu.addAction(test(DisplayThumbnails)
                                                      ? tr("Hide t&humbnail")
                                                      : tr("Display t&humbnail"));

    const QAction *hidePointerAction = menu.addAction(test(HidePointer)
                                                      ? tr("Display &cursor")
                                                      : tr("Hide &cursor"));

    menu.addSeparator();
    QAction *doToggleRemoveImage = 0;
    QAction *doPurge = 0;
    if (!d.data.isEmpty()) {
        if (d.toDelete.contains(d.data.at(d.current))) {
            doToggleRemoveImage = menu.addAction(tr("Undelete image"));
        } else {
            doToggleRemoveImage = menu.addAction(tr("Delete image"));
        }
        if (!d.toDelete.isEmpty()) {
            doPurge = menu.addAction(tr("Purge removed images"));
        }
    }
    menu.addSeparator();
    QAction *doShowNormal = menu.addAction(tr("Show &normal"));
    QAction *doShowFullScreen = menu.addAction(tr("Show f&ull screen"));
    QAction *doShowMaxized = menu.addAction(tr("Show &maximized"));
    QAction *slideAct = menu.addAction(d.slideShowTimer.isActive()
                                       ? tr("&Stop slideshow")
                                       : tr("&Start slideshow"));
    QAction *autoZoomAct = menu.addAction(test(AutoZoomEnabled)
                                          ? tr("&Turn off autozoom")
                                          : tr("&Turn on autozoom"));
    menu.addSeparator();
    QMenu *colorMenu = menu.addMenu(tr("Background color"));
    colorMenu->addAction(tr("Grid"))->setData("yellow|black");
    colorMenu->addAction(tr("Black"))->setData("green|yellow");
    colorMenu->addAction(tr("Red"))->setData("black|yellow");
    colorMenu->addAction(tr("Green"))->setData("black|yellow");
    colorMenu->addAction(tr("Blue"))->setData("yellow|black");
    colorMenu->addAction(tr("Yellow"))->setData("black|yellow");
    colorMenu->addAction(tr("Gray"))->setData("yellow|black");
    if (windowState() & Qt::WindowFullScreen) {
        doShowFullScreen->setEnabled(false);
    } else if (windowState() & Qt::WindowMaximized) {
        doShowMaxized->setEnabled(false);
    } else {
        doShowNormal->setEnabled(false);
    }
    menu.addSeparator();
    QAction *copy = d.data.isEmpty() ? 0 : menu.addAction(tr("&Copy: '%1'").
                                                          arg(d.data.at(d.current)->path));
    menu.addAction("About vp2", this, SLOT(about()));
    menu.addSeparator();
    const QAction *quit = menu.addAction(tr("&Quit"));
    QAction *ret = menu.exec(e->globalPos());
    if (!ret)
        return;
    if (ret == hidePointerAction) {
        toggle(HidePointer);
        QSettings().setValue("hidePointer", test(HidePointer));
        viewport()->setCursor(QCursor(test(HidePointer) ? Qt::BlankCursor : Qt::ArrowCursor));
    } else if (ret == addFiles) {
        addImages();
    } else if (ret == addDirs) {
        addDirectory();
    } else if (ret == addDirsRecursively) {
        addDirectoryRecursively();
    } else if (ret == displayFileNameAct) {
        toggle(DisplayFileName);
        QSettings().setValue("displayFileName", test(DisplayFileName));
        viewport()->update(textArea());
    } else if (ret == displayThumbnails) {
        toggle(DisplayThumbnails);
        QSettings().setValue("displayThumbnails", test(DisplayThumbnails));
        viewport()->update(textArea());
        updateAreas();
    } else if (ret == doShowNormal) {
        showNormal();
    } else if (ret == doShowMaxized) {
        showMaximized();
    } else if (ret == doShowFullScreen) {
        showFullScreen();
    } else if (ret == copy) {
        QClipboard *clip = qApp->clipboard();
        QString path = d.data.at(d.current)->path;
        if (path.contains(' ')) {
            // ### check for "
            path.prepend('"');
            path.append('"');
        }

        if (clip->supportsSelection()) {
            clip->setText(path, QClipboard::Selection);
        }
        clip->setText(path, QClipboard::Clipboard);
    } else if (ret == quit) {
        close();
    } else if (ret == slideAct) {
        toggleSlideShow();
    } else if (ret == autoZoomAct) {
        toggleAutoZoom();
    } else if (ret->parent() == colorMenu) {
        QSettings().setValue("bgcol", ret->text().toLower());
        setBackgroundColor(ret->text().toLower());
    } else if (ret == doToggleRemoveImage) {
        toggleRemoveCurrentImage();
    } else if (ret == doPurge) {
        purge();
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

        fprintf(stderr, "%s\n", qPrintable(usage));
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
        if (e->delta() < 0) {
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
            if (dt->image.isNull()) {
                drawText(&p, eventRect, viewportRect, Qt::AlignCenter, fm, "Loading " + QFileInfo(dt->path).fileName());
            } else {
                const QSize pixmapSize = dt->image.size();
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
                if (eventRect.isNull() || eventRect.intersects(r))
                    p.drawImage(r, dt->image);
                if (test(DisplayThumbnails) && d.data.size() > 1) {
                    const int thumbWidth = qMin(pixmapSize.width(), qMax(d.thumbMinWidth, r.left() - 2));
                    // qDebug() << pixmapSize << thumbWidth << r;
                    ThumbInfo *thumbs[] = { &d.thumbLeft, &d.thumbRight };
                    for (int i=0; i<2; ++i) {
                        const int idx = bound(d.current + (i == 0 ? -1 : 1));
                        if (thumbWidth != thumbs[i]->image.width()
                            && !d.data.at(idx)->image.isNull()
                            && (!thumbs[i]->thread || thumbs[i]->requestedWidth != thumbWidth)) {
                            thumbs[i]->thread = new ThumbLoaderThread(d.data.at(idx)->image, thumbWidth);
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
                         dt->path + QString("\n%1 of %2 (%3 images in memory) (%4 in loading queue)").
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
        size = viewport()->size();
        if (!dt->image.isNull()) {
            if (!isVisible() || rightSize(dt->image.size(), size))
                return;
        }
    } else if (!dt->image.isNull()) {
        return;
    }
    d.loading[dt] = index;
#if 0
    if (dt->path.endsWith(".pdf", Qt::CaseInsensitive)) {
        Magick::Image pdf(dt->path.toStdString());
        if (pdf.isValid()) {
            if (!size.isNull()) {
                QSize s(pdf.columns(), pdf.rows());
                s.scale(size, Qt::KeepAspectRatio);
                pdf.resize(Magick::Geometry(s.width(), s.height()));
            }
            if (dt->image.isNull())
                ++d.imagesInMemory;

            QImage image(pdf.columns(), pdf.rows(), QImage::Format_RGB32);
            // pdf.write(0, 0, image.width(), image.height(), "RGB", Magick::IntegerPixel, image.bits());
            pdf.write(0, 0, image.width(), image.height(), "RGB", Magick::CharPixel, image.bits());
            onImageLoaded(dt, image);
        }
    } else
#endif
    {
        d.imageLoaderThread.load(dt->path, flags, dt->rotation, dt, size);
    }
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
        while (QFontMetrics(f).width(d.longestPath) >= w && f.pixelSize() > 10) {
            f.setPixelSize(f.pixelSize() - 1);
        }
        if (d.fontSize != f.pixelSize()) {
            d.fontSize = f.pixelSize();
            if (d.data.isEmpty() || d.data.at(d.current)->image.isNull()) {
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
#define FIND_SIZE(arg)                                  \
    qint64 &size_ ## arg = size[arg];                   \
    if (size_ ## arg == 0) {                            \
        size_ ## arg = QFileInfo(arg->path).size();     \
    }

    FIND_SIZE(left);
    FIND_SIZE(right);
#undef FIND_SIZE
    return size_left > size_right;
}

static inline bool compareDataByCreationDate(const Data *left, const Data *right)
{
    static QHash<const Data*, uint> date;
#define FIND_DATE(arg)                                                  \
    uint &date_ ## arg = date[arg];                                     \
    if (date_ ## arg == 0) {                                            \
        date_ ## arg = QFileInfo(arg->path).created().toTime_t();       \
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
            it = qLowerBound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataNaturally);
            break;
        case Alphabetically:
            it = qLowerBound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataAlphabetically);
            break;
        case Size:
            it = qLowerBound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataBySize);
            break;
        case CreationDate:
            it = qLowerBound<DataIterator>(d.data.begin(), d.data.end(), dt, compareDataByCreationDate);
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
}

void Window::toggleAutoZoom()
{
    toggle(AutoZoomEnabled);
    updateImages();
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

void Window::onImageLoaded(void *userData, const QImage &image)
{
    static const bool verbose = (qgetenv("VP2_VERBOSE") == "1");
    Data *dt = reinterpret_cast<Data*>(userData);
    const int idx = d.loading.value(dt, -1);
    d.loading.remove(dt);
    if (verbose) {
        qDebug() << "got image" << idx << "current" << d.current << d.loading.values();
    }

    if (idx == -1)
        return;

    if (dt->image.isNull())
        ++d.imagesInMemory;
    dt->image = image;

    if (idx == d.current) {
        if (!rightSize(image.size(), viewport()->size())) {
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
}

void Window::toggleCursorVisible()
{
    viewport()->setCursor(QCursor(toggle(HidePointer) ? Qt::BlankCursor : Qt::ArrowCursor));
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
        if (!d.data.at(i)->image.isNull())
            it->setData(2, Qt::DecorationRole, d.data.at(i)->image.scaled(40, 40));
        if (i == d.current) {
            tw->setItemSelected(it, true);
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
    updateAreas();
    viewport()->update();
}

void Window::toggleShowFileName()
{
    toggle(DisplayFileName);
    updateAreas();
    viewport()->update();
}

void Window::keyPressEvent(QKeyEvent *e)
{
    restartQuitTimer();
    switch (e->key()) {
    case Qt::Key_BracketLeft:
        rotateLeft();
        break;
    case Qt::Key_BracketRight:
        rotateRight();
        break;
    case Qt::Key_Less:
        previousPage();
        break;
    case Qt::Key_Greater:
        nextPage();
        break;
    case Qt::Key_Home:
        home();
        break;
    case Qt::Key_End:
        end();
        break;
    case Qt::Key_Slash:
        startSearch();
        break;
    case Qt::Key_C:
        if (e->modifiers() == Qt::NoModifier) {
            toggleCursorVisible();
        } else if (e->modifiers() == Qt::ControlModifier) {
            copyPath();
        }
        break;
    case Qt::Key_I:
        if (e->modifiers() & Qt::ControlModifier) {
            showInfo();
        }
        break;
    case Qt::Key_H:
        toggleShowThumbnails();
        break;
    case Qt::Key_T:
        if (e->modifiers() == Qt::ShiftModifier) {
            static const Qt::GlobalColor colors[] = { Qt::white, Qt::black, Qt::yellow, Qt::green, Qt::cyan, Qt::transparent };
            static int idx = 0;
            if (colors[++idx] == Qt::transparent)
                idx = 0;
            d.penColor = colors[idx];
            viewport()->update();
        } else {
            toggleShowFileName();
        }
        break;
    case Qt::Key_Space:
        if (d.slideShowTimer.isActive()) {
            toggleSlideShow();
        } else if (e->modifiers() & (Qt::ControlModifier|Qt::AltModifier)) {
            nextDirectory(e->modifiers() & Qt::ShiftModifier ? -1 : 1);
        } else if (e->modifiers() & Qt::ShiftModifier) {
            moveCurrentIndexBy(-1);
        } else {
            moveCurrentIndexBy(1);
        }
        break;
    case Qt::Key_Left:
        if (e->modifiers() & Qt::AltModifier) {
            back();
            break;
        }
        // fallthrough
    case Qt::Key_Up:
        moveCurrentIndexBy(e->modifiers() & Qt::ControlModifier ? -10 : -1);
        break;
    case Qt::Key_Right:
        if (e->modifiers() & Qt::AltModifier) {
            forward();
            break;
        }

    case Qt::Key_Down:
        moveCurrentIndexBy(e->modifiers() & Qt::ControlModifier ? 10 : 1);
        break;
    case Qt::Key_S:
        if (e->modifiers() == Qt::NoModifier) {
            toggleSlideShow();
        }
        break;
    case Qt::Key_Plus:
        d.slideShowInterval *= 0.9;
        d.slideShowTimer.start(int(d.slideShowInterval * 1000.0), this);
        break;
    case Qt::Key_Minus:
        d.slideShowInterval *= 1.1;
        d.slideShowTimer.start(int(d.slideShowInterval * 1000.0), this);
        break;
    case Qt::Key_F:
        if (e->modifiers() == Qt::NoModifier) {
            if (windowState() & Qt::WindowFullScreen) {
                showNormal();
            } else {
                showFullScreen();
            }
        } else if (e->modifiers() == Qt::ShiftModifier && d.current != -1) {
            printf("%s\n", qPrintable(d.data.at(d.current)->path));
        }
        break;
    case Qt::Key_N:
    case Qt::Key_P:
        if ((e->key() == Qt::Key_N) == (e->modifiers() == Qt::NoModifier)) {
            searchNext();
        } else {
            searchPrevious();
        }
        break;
    case Qt::Key_X:
        if (e->modifiers() == Qt::NoModifier) {
            showMaximized();
        }
        break;
    case Qt::Key_Z:
        if (e->modifiers() == Qt::ShiftModifier) {
            toggleAutoZoom();
        } else if (d.data.size() > 1) {
            if (e->modifiers() == Qt::ControlModifier && !d.lineEdit->text().isEmpty()) {
                int count = rand() % (d.data.size() / 10);
                while (count--)
                    searchNext();
            } else if (e->modifiers() == Qt::NoModifier)  {
                const int index = rand() % d.data.size();
                setCurrentIndex(index);
            }
        }

        break;
    case Qt::Key_R:
        if (e->modifiers() & Qt::AltModifier) {
            addDirectoryRecursively();
        } else {
            showNormal();
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        toggleRemoveCurrentImage();
        break;

    case Qt::Key_D:
        if (e->modifiers() & Qt::ControlModifier) {
            toggleRemoveCurrentImage();
        } else {
            addDirectory();
        }
        break;
    case Qt::Key_U:
        if (e->modifiers() & Qt::ControlModifier) {
            undeleteCurrentImage();
        } else {
            viewport()->update();
        }
        break;
    case Qt::Key_O:
    case Qt::Key_L:
        addImages();
        break;
    case Qt::Key_Q:
        close();
        break;
    case Qt::Key_0:
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9: {
        if (d.data.isEmpty() || e->text().isEmpty())
            return;
        d.indexBuffer.append(e->text());
        forever {
            bool ok;
            int i = d.indexBuffer.toInt(&ok) - 1; // indexes are 0-indexed
            Q_ASSERT(ok);
            if (i >= d.data.size()) {
                d.indexBuffer.remove(0, 1);
            } else {
                d.indexBufferTimer.start(300, this);
                break;
            }
        }
        if (!d.indexBuffer.isEmpty())
            d.indexBufferClearTimer.start(2000, this);
        return;
    }
    case Qt::Key_Escape:
        if (d.indexBuffer.isEmpty()) {
            close();
        } else {
            d.indexBuffer.clear();
            d.indexBufferTimer.stop();
        }
    }
    d.indexBuffer.clear();
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
    d.areas[Center] = d.data.at(d.current)->image.rect();
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
    const QSize s = d.current == -1 ? QSize() : d.data.at(d.current)->image.size();
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
    const int old = d.current;
    searchNext();
    if (old != d.current) {
        d.lineEdit->hide();
    } else {
        d.lineEdit->setStyleSheet("background: red");
        QTimer::singleShot(1000, this, SLOT(resetLineEditStyleSheet()));
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

    if (d.current != -1 && !d.data.at(d.current)->image.isNull()) {
        string += QString("%1 x %2\n").arg(d.data.at(d.current)->image.width()).arg(d.data.at(d.current)->image.height());
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
inline QDebug operator<<(QDebug debug, const QLinkedList<T> &list)
{
    debug.nospace() << "(";
    bool first = true;
    for (Q_TYPENAME QLinkedList<T>::const_iterator i = list.begin(); i != list.end(); ++i) {
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
        if (test(AutoZoomEnabled)) {
            QSize s = reader.size();
            if (s != viewport()->size()) {
                s.scale(viewport()->size(), Qt::KeepAspectRatio);
                reader.setScaledSize(s);
            }
        }
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


void Window::rotateLeft()
{
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
    for (QLinkedList<int>::iterator i = d.history.begin(); i != d.history.end(); ++i) {
        if (*i >= index)
            (*i) += added;
    }
}
