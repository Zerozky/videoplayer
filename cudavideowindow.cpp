#include "cudavideowindow.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

CudaVideoWindow::CudaVideoWindow(QWindow *parent)
    : QOpenGLWindow(NoPartialUpdate, parent)
#ifdef Q_OS_WIN
    , m_texture(0)
    , m_cudaResource(nullptr)
#endif
    , m_frameWidth(0)
    , m_frameHeight(0)
{
    // 如果需要嵌入到 QWidget 中，后面可以用 createWindowContainer() 包裹本窗口
}

CudaVideoWindow::~CudaVideoWindow()
{
    makeCurrent();
#ifdef Q_OS_WIN
    if (m_cudaResource) {
        cudaGraphicsUnregisterResource(m_cudaResource);
        m_cudaResource = nullptr;
    }
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if(m_stream) {
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
    }
#endif
    doneCurrent();
}

void CudaVideoWindow::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);
#ifdef Q_OS_WIN
    // 创建 OpenGL 纹理
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 先分配一个 1x1 纹理
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    qDebug() << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    qDebug() << reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    // 创建 CUDA 流
    cudaError_t err = cudaStreamCreate(&m_stream);
    if (err != cudaSuccess) {
        qDebug() << "cudaStreamCreate failed:" << cudaGetErrorString(err);
    }

    // 注册该纹理到 CUDA
    err = cudaGraphicsGLRegisterImage(&m_cudaResource, m_texture, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard);
    if (err != cudaSuccess) {
        qDebug() << "cudaGraphicsGLRegisterImage failed:" << cudaGetErrorString(err);
    } else {
        qDebug() << "CUDA resource registered successfully.";
    }
    // 注意：QOpenGLWindow 不会使用 FBO 进行离屏渲染
#endif
}

void CudaVideoWindow::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
#ifdef Q_OS_WIN
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    // 计算当前窗口的宽高比和视频的宽高比
    float windowAspect = static_cast<float>(width()) / height();
    float videoAspect = (m_frameHeight > 0) ? static_cast<float>(m_frameWidth) / m_frameHeight : 1.0f;

    // 默认全屏时，四边形覆盖 [-1, 1] 区域，即宽高均为2
    float quadWidth = 2.0f;
    float quadHeight = 2.0f;
    // 根据窗口和视频的宽高比，调整四边形尺寸，确保视频按原比例显示
    if (windowAspect > videoAspect) {
        // 窗口更宽，视频高度充满，高度为2，宽度按比例缩放
        quadWidth = 2.0f * videoAspect / windowAspect;
    } else {
        // 窗口更窄，视频宽度充满，宽度为2，高度按比例缩放
        quadHeight = 2.0f * windowAspect / videoAspect;
    }
    float halfW = quadWidth / 2.0f;
    float halfH = quadHeight / 2.0f;

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-halfW, -halfH);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( halfW, -halfH);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( halfW,  halfH);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-halfW,  halfH);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void CudaVideoWindow::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void CudaVideoWindow::updateFrame(const uint8_t *data, int width, int height)
{
#ifdef Q_OS_WIN
    QMutexLocker locker(&m_mutex);
    // 如果尺寸变化，则重新分配纹理
    if (width != m_frameWidth || height != m_frameHeight) {
        m_frameWidth = width;
        m_frameHeight = height;

        // 如果已有注册的 CUDA 资源，则先取消注册
        if (m_cudaResource) {
            cudaGraphicsUnregisterResource(m_cudaResource);
            m_cudaResource = nullptr;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_frameWidth, m_frameHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        // 重新注册该纹理到 CUDA
        cudaError_t err = cudaGraphicsGLRegisterImage(&m_cudaResource, m_texture, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard);
        if (err != cudaSuccess) {
            qDebug() << "Re-registration of CUDA resource failed:" << cudaGetErrorString(err);
            return;
        }
    }
    if (!m_cudaResource) {
        qDebug() << "m_cudaResource is invalid (null)";
        return;
    }
    cudaError_t err = cudaGraphicsMapResources(1, &m_cudaResource, 0);
    if (err != cudaSuccess) {
        qDebug() << "cudaGraphicsMapResources failed:" << cudaGetErrorString(err);
        return;
    }
    cudaArray_t array;
    err = cudaGraphicsSubResourceGetMappedArray(&array, m_cudaResource, 0, 0);
    if (err != cudaSuccess) {
        qDebug() << "cudaGraphicsSubResourceGetMappedArray failed:" << cudaGetErrorString(err);
        cudaGraphicsUnmapResources(1, &m_cudaResource, 0);
        return;
    }
    size_t pitch = m_frameWidth * 4; // 每像素4字节（RGBA）
    err = cudaMemcpy2DToArrayAsync(array, 0, 0,
                                   data, pitch, pitch, m_frameHeight,
                                   cudaMemcpyHostToDevice, m_stream);
    if (err != cudaSuccess) {
        qDebug() << "cudaMemcpy2DAsync failed:" << cudaGetErrorString(err);
    }
    cudaStreamSynchronize(m_stream);
    cudaGraphicsUnmapResources(1, &m_cudaResource, 0);
    update(); // 触发重绘
#endif
}

void CudaVideoWindow::clearScreen()
{
#ifdef Q_OS_WIN
    makeCurrent();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    doneCurrent();
    update();
#endif
}
