#include <QApplication>
#include "mediawindow.h"
#undef main
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MediaWindow *mediaWindow = new MediaWindow();
    mediaWindow->show();

    return a.exec();
}
