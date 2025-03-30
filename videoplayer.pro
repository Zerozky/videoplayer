QMAKE_PROJECT_DEPTH = 0

QT       += core gui charts multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    cudavideowidget.cpp \
    cudavideowindow.cpp \
    demuxworker.cpp \
    main.cpp \
    mainwindow.cpp \
    mediawindow.cpp

HEADERS += \
    cudavideowidget.h \
    cudavideowindow.h \
    demuxworker.h \
    mainwindow.h \
    mediawindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32:!msvc {
    INCLUDEPATH += D:\ffmpeg\ffmpeg-7.1-full_build-shared\include
    INCLUDEPATH += D:\SDL\SDL2\SDL2-2.32.0\x86_64-w64-mingw32\include

    LIBS += -LD:\ffmpeg\ffmpeg-7.1-full_build-shared\lib -lswresample -lavformat -lswscale -lavutil  -lavcodec  -lavdevice -lavfilter
    LIBS += -LD:\SDL\SDL2\SDL2-2.32.0\x86_64-w64-mingw32\lib -lSDL2 -lSDL2main
    LIBS += -lpthread
}

macx {
    INCLUDEPATH += /opt/homebrew/include

    LIBS += -L/opt/homebrew/lib -lswresample -lavformat -lswscale -lavutil  -lavcodec  -lavdevice -lavfilter
    LIBS += -L/opt/homebrew/lib -lSDL2 -lSDL2main
    LIBS += -lpthread
    LIBS += -framework VideoToolbox -framework CoreMedia -framework CoreVideo
}

win32-msvc {
    CUDA_DIR = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8"
    FFMPEG_DIR = "D:/ffmpeg/ffmpeg-7.1-full_build-shared"

    INCLUDEPATH += $$FFMPEG_DIR/include
    INCLUDEPATH += $$CUDA_DIR/include

    LIBS += "$$FFMPEG_DIR/lib/avcodec.lib" \
            "$$FFMPEG_DIR/lib/avformat.lib" \
            "$$FFMPEG_DIR/lib/avutil.lib" \
            "$$FFMPEG_DIR/lib/avdevice.lib" \
            "$$FFMPEG_DIR/lib/avfilter.lib" \
            "$$FFMPEG_DIR/lib/swresample.lib" \
            "$$FFMPEG_DIR/lib/swscale.lib"
    LIBS += "$$CUDA_DIR/lib/x64/cudart.lib"
    LIBS += "opengl32.lib"
}
