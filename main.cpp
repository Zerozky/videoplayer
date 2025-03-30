#include <QApplication>
#include <QSurfaceFormat>
#include "mediawindow.h"
#undef main
int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);  // 只输出错误级别的日志

    QSurfaceFormat format;
    format.setProfile(QSurfaceFormat::CompatibilityProfile); // 设置为兼容模式
    format.setVersion(3, 3); // 根据需要设置版本
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);

    qDebug() << "App Run.";
    MediaWindow *mediaWindow = new MediaWindow();
    mediaWindow->show();

    return a.exec();
}
