#include <QApplication>
#include "mediawindow.h"
#undef main
int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);  // 只输出错误级别的日志

    QApplication a(argc, argv);

    qDebug() << "App Run.";
    MediaWindow *mediaWindow = new MediaWindow();
    mediaWindow->show();

    return a.exec();
}
