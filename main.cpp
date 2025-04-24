#include <QApplication>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QSettings>
#include <QProcess>
#include "mediawindow.h"
#undef main
int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);  // 只输出错误级别的日志

    QApplication a(argc, argv);

    // 从配置里读语言，默认 zh_CN
    QSettings settings("MyCompany", "MyApp");
    QString locale = settings.value("AppLanguage", "zh_CN").toString();

    // 全局 Translator 对象
    static QTranslator translator;
    // 默认加载简体中文
    translator.load(QString(":/i18n/app_%1").arg(locale));
    a.installTranslator(&translator);

    qDebug() << "App Run.";
    MediaWindow *mediaWindow = new MediaWindow();
    mediaWindow->show();

    // 监听语言切换
    QObject::connect(mediaWindow->controlDialog, &ControlPanelDialog::languageChanged, &a, [&](const QString &locale){
        // 1) 存下用户的选择
        QSettings settings("MyCompany", "MyApp");
        settings.setValue("AppLanguage", locale);

        // 2) 用同样的参数重启自己
        QString exe = QCoreApplication::applicationFilePath();
        QStringList args = QCoreApplication::arguments();
        QProcess::startDetached(exe, args);

        // 3) 退出当前进程
        a.quit();
    });

    return a.exec();
}
