QT       += core gui charts multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    demuxworker.cpp \
    main.cpp \
    mainwindow.cpp \
    mediawindow.cpp

HEADERS += \
    demuxworker.h \
    mainwindow.h \
    mediawindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH += D:\ffmpeg\ffmpeg-7.1-full_build-shared\include
INCLUDEPATH += D:\SDL\SDL2\SDL2-2.32.0\x86_64-w64-mingw32\include

LIBS += -LD:\ffmpeg\ffmpeg-7.1-full_build-shared\lib -lswresample -lavformat -lswscale -lavutil  -lavcodec  -lavdevice -lavfilter
LIBS += -LD:\SDL\SDL2\SDL2-2.32.0\x86_64-w64-mingw32\lib -lSDL2 -lSDL2main
LIBS += -lpthread
