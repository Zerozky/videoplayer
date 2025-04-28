#include "titlebar.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

TitleBar::TitleBar(QWidget *parent)
    : QWidget{parent}, m_dragging(false)
{
    setFixedHeight(24);

    // 水平布局 关闭/最小化/最大化
    auto *hbox = new QHBoxLayout(this);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
    // 关闭
    auto *closeBtn = new QPushButton(QIcon(":/icons/close"), "", this);
    closeBtn->setObjectName("closeButton");
    hbox->addWidget(closeBtn);
    // 最小化
    auto *minBtn = new QPushButton(QIcon(":/icons/yellow"), "", this);
    minBtn->setObjectName("minimizeButton");
    hbox->addWidget(minBtn);
    // 最大化/还原
    auto *maxBtn = new QPushButton(QIcon(":/icons/max"), "", this);
    maxBtn->setObjectName("maximizeButton");
    hbox->addWidget(maxBtn);

    hbox->addStretch();
    auto *titleLabel = new QLabel(window()->windowTitle(), this);
    titleLabel->setObjectName("titleLabel");
    hbox->addWidget(titleLabel);

    // 信号连接
    connect(minBtn, &QPushButton::clicked, this, &TitleBar::sigMinimized);
    connect(maxBtn, &QPushButton::clicked, this, &TitleBar::sigtoggleMaximized);
    connect(closeBtn, &QPushButton::clicked, this, &TitleBar::sigclose);
    connect(window(), &QWidget::windowTitleChanged,
            titleLabel, &QLabel::setText);

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
