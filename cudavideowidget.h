#ifndef CUDAVIDEOWIDGET_H
#define CUDAVIDEOWIDGET_H

#include <QObject>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMutex>

#ifdef Q_OS_WIN
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

class CudaVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit CudaVideoWidget(QWidget *parent = nullptr);
    ~CudaVideoWidget();

    // 用于更新视频帧，data数据格式要求为RGBA（每像素4字节）
    void updateFrame(const uint8_t *data, int width, int height);

    void clearScreen();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
#ifdef Q_OS_WIN
    GLuint m_texture;                    // OpenGL 纹理 ID
    cudaGraphicsResource *m_cudaResource; // CUDA 注册的资源
#endif
    QMutex m_mutex;   // 用于线程同步
    int m_frameWidth; // 当前帧宽度
    int m_frameHeight;// 当前帧高度

};

#endif // CUDAVIDEOWIDGET_H
