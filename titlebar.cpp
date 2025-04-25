#include "titlebar.h"

TitleBar::TitleBar(QWidget *parent)
    : QWidget{parent}, m_dragging(false)
{
    setFixedHeight(24);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging   = true;
        // 记录相对父窗口左上角的偏移
        m_dragPos    = event->globalPos() - window()->frameGeometry().topLeft();
        event->accept();
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        // 移动父窗口
        window()->move(event->globalPos() - m_dragPos);
        event->accept();
    }
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_dragging = false;
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    // 双击切换最大化/还原
    QWidget *w = window();
    if (w->isMaximized()) w->showNormal();
    else                  w->showMaximized();
}
