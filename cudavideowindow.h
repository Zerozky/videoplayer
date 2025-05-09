#ifndef CUDAVIDEOWINDOW_H
#define CUDAVIDEOWINDOW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMutex>
#ifdef Q_OS_WIN
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

class CudaVideoWindow : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit CudaVideoWindow(QWidget *parent = nullptr);
    ~CudaVideoWindow();

    // 更新纹理数据，要求 data 格式为 RGBA（每像素4字节）
    void updateFrame(const uint8_t *data, int width, int height);
    // 清空显示（黑屏）
    void clearScreen();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
#ifdef Q_OS_WIN
    GLuint m_texture;   // OpenGL 2D 纹理ID，在 GPU 上存储视频帧
    cudaGraphicsResource *m_cudaResource;   // CUDA 注册的资源，实现 CUDA OpenGL 互操作
    cudaStream_t m_stream = nullptr;    // CUDA 异步流
#endif
    QMutex m_mutex; // 用于线程同步
    int m_frameWidth;   // 当前帧宽度
    int m_frameHeight;  // 当前帧高度
};


#endif // CUDAVIDEOWINDOW_H
