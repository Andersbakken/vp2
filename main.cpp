#include "window.h"
#ifdef MAGICK_ENABLED
#include <Magick++/Image.h>
#endif

// ### don't call updateImages from all over the place


int main(int argc, char **argv)
{
#ifdef MAGICK_ENABLED
#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
    Magick::InitializeMagick(*argv);
#endif
#endif
    QApplication a(argc, argv);
    a.setApplicationName("vp2");
    a.setOrganizationName("AndersSoft");
    Window w(a.arguments());
    const int ret = a.exec();
    return ret;
}
