#include "window.h"

int main(int argc, char **argv)
{
    QApplication a(argc, argv);
    a.setApplicationName("vp2");
    a.setOrganizationName("AndersSoft");
    Window w(a.arguments());
    const int ret = a.exec();
    return ret;
}
