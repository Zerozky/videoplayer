#include <QApplication>
#include <QSurfaceFormat>
#include "mediawindow.h"
#undef main
int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);  // 只输出错误级别的日志

    QApplication a(argc, argv);

    // // 设置 OpenGL 上下文格式为兼容模式
    // QSurfaceFormat format;
    // format.setVersion(3, 3); // 设置为你需要的 OpenGL 版本（例如 3.3）
    // format.setProfile(QSurfaceFormat::CompatibilityProfile);
    // QSurfaceFormat::setDefaultFormat(format);

    qDebug() << "App Run.";
    MediaWindow *mediaWindow = new MediaWindow();
    mediaWindow->show();

    return a.exec();
}
