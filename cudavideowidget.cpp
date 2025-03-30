#include "cudavideowidget.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

CudaVideoWidget::CudaVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
#ifdef Q_OS_WIN
    , m_texture(0), m_cudaResource(nullptr)
#endif
    , m_frameWidth(0), m_frameHeight(0)
{
    setAttribute(Qt::WA_NativeWindow);
}

CudaVideoWidget::~CudaVideoWidget()
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
#endif
    doneCurrent();
}

void CudaVideoWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);

    // 调试输出
    QSurfaceFormat fmt = context()->format();
    qDebug() << "OpenGL version:" << fmt.majorVersion() << "." << fmt.minorVersion();
    qDebug() << "Profile:" << (fmt.profile() == QSurfaceFormat::CoreProfile ? "Core" : "Compatibility");

#ifdef Q_OS_WIN
    // 创建 OpenGL 纹理
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 初始分配一个 1x1 的纹理
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 注册 OpenGL 纹理到 CUDA
    cudaError_t err = cudaGraphicsGLRegisterImage(&m_cudaResource, m_texture, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard);
    if (err != cudaSuccess) {
        qDebug() << "cudaGraphicsGLRegisterImage failed:" << cudaGetErrorString(err);
    } else {
        qDebug() << "CUDA resource registered successfully.";
    }

    // 连接当前上下文即将销毁的信号，确保注销 CUDA 资源
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this](){
        if (m_cudaResource) {
            cudaGraphicsUnregisterResource(m_cudaResource);
            m_cudaResource = nullptr;
            qDebug() << "CUDA resource unregistered due to context destruction.";
        }
    });
#endif
}

void CudaVideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
#ifdef Q_OS_WIN
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // 使用简单的固定功能管线绘制纹理覆盖整个窗口
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
#else
    // 对于非 Windows 平台，采用其他兼容方案（比如原来的软件转换显示）
#endif
}

void CudaVideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void CudaVideoWidget::updateFrame(const uint8_t *data, int width, int height)
{
#ifdef Q_OS_WIN
    QMutexLocker locker(&m_mutex);
    if (!m_cudaResource) {
        qDebug() << "m_cudaResource is invalid (null)";
        return;
    }
    // 如果输入帧尺寸发生变化，则重新分配纹理
    if (width != m_frameWidth || height != m_frameHeight) {
        m_frameWidth = width;
        m_frameHeight = height;
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_frameWidth, m_frameHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    // 映射 CUDA 资源
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
    // 将输入数据拷贝到纹理对应的 CUDA 数组
    // 这里假设输入 data 已经为 RGBA 格式，每像素4字节
    size_t pitch = m_frameWidth * 4;
    err = cudaMemcpy2DToArray(array, 0, 0, data, pitch, pitch, m_frameHeight, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) {
        qDebug() << "cudaMemcpy2DToArray failed:" << cudaGetErrorString(err);
    }
    cudaGraphicsUnmapResources(1, &m_cudaResource, 0);
    // 触发重绘
    update();
#else
    // 非 Windows 平台暂时走其他方案
#endif
}

void CudaVideoWidget::clearScreen()
{
    makeCurrent();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    doneCurrent();
    update();
}
