#include "controlpaneldialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>

ControlPanelDialog::ControlPanelDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Control Panel"));
    setModal(false);
    // 整体布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // — 第一行：快退1分、快退5秒、快进5秒、快进1分
    QHBoxLayout *row1 = new QHBoxLayout;
    QPushButton *rew1m = new QPushButton(tr("<< 1m"), this);
    QPushButton *rew5s = new QPushButton(tr("<< 5s"), this);
    QPushButton *fwd5s = new QPushButton(tr("5s >>"), this);
    QPushButton *fwd1m = new QPushButton(tr("1m >>"), this);
    row1->addWidget(rew1m);
    row1->addWidget(rew5s);
    row1->addWidget(fwd5s);
    row1->addWidget(fwd1m);
    mainLayout->addLayout(row1);

    // 连接信号
    connect(rew1m, &QPushButton::clicked, this, [=]{ emit seekRelative(-60.0); });
    connect(rew5s, &QPushButton::clicked, this, [=]{ emit seekRelative(-5.0); });
    connect(fwd5s, &QPushButton::clicked, this, [=]{ emit seekRelative( 5.0); });
    connect(fwd1m, &QPushButton::clicked, this, [=]{ emit seekRelative( 60.0); });

    // — 第二行：倍速按钮 + 当前倍速显示
    QHBoxLayout *row2 = new QHBoxLayout;
    QPushButton *btn05x = new QPushButton(tr("0.5×"), this);
    QPushButton *btn1x = new QPushButton(tr("1×"), this);
    QPushButton *btn15x = new QPushButton(tr("1.5×"), this);
    QPushButton *btn3x = new QPushButton(tr("3×"), this);
    QLabel    *lblSpeed = new QLabel(tr("Speed: 1.0×"), this);
    row2->addWidget(btn05x);
    row2->addWidget(btn1x);
    row2->addWidget(btn15x);
    row2->addWidget(btn3x);
    row2->addStretch();
    row2->addWidget(lblSpeed);
    mainLayout->addLayout(row2);

    auto updateSpeedLabel = [lblSpeed](double s){
        lblSpeed->setText(QString(tr("Speed: %1×")).arg(s));
    };
    connect(btn05x,  &QPushButton::clicked, this, [=]{ emit setPlaybackSpeed(0.5);  updateSpeedLabel(0.5);  });
    connect(btn1x,  &QPushButton::clicked, this, [=]{ emit setPlaybackSpeed(1.0);  updateSpeedLabel(1.0);  });
    connect(btn15x, &QPushButton::clicked, this, [=]{ emit setPlaybackSpeed(1.5);  updateSpeedLabel(1.5);  });
    connect(btn3x,  &QPushButton::clicked, this, [=]{ emit setPlaybackSpeed(3.0);  updateSpeedLabel(3.0);  });

    // — 第三行：ASS 字幕字体、字号
    QHBoxLayout *row3 = new QHBoxLayout;
    QFontComboBox *fontBox = new QFontComboBox(this);
    QSpinBox      *sizeBox = new QSpinBox(this);
    sizeBox->setRange(8, 72);
    sizeBox->setValue(20);
    row3->addWidget(new QLabel(tr("Font:"), this));
    row3->addWidget(fontBox);
    row3->addWidget(new QLabel(tr("Size:"), this));
    row3->addWidget(sizeBox);
    mainLayout->addLayout(row3);

    connect(fontBox, &QFontComboBox::currentFontChanged,
            this, &ControlPanelDialog::subtitleFontChanged);
    connect(sizeBox, &QSpinBox::valueChanged,
            this, &ControlPanelDialog::subtitleFontSizeChanged);

    // 设置固定宽度，方便 UI 排列
    setFixedWidth(500);

    QHBoxLayout *rowLang = new QHBoxLayout;
    rowLang->addWidget(new QLabel(tr("Language:"), this));
    QComboBox *comboLang = new QComboBox(this);
    comboLang->addItem(QStringLiteral("简体中文"), "zh_CN");
    comboLang->addItem(QStringLiteral("English"), "en_US");
    comboLang->addItem(QStringLiteral("日本語"), "ja_JP");
    // 读取上次保存的语言
    QSettings settings("MyCompany", "MyApp");
    QString currentLocale = settings.value("AppLanguage", "zh_CN").toString();
    // 把下拉框选中到当前语言
    int idx = comboLang->findData(currentLocale);
    if (idx >= 0) {
        comboLang->setCurrentIndex(idx);
    }
    rowLang->addWidget(comboLang);
    mainLayout->addLayout(rowLang);

    connect(comboLang, QOverload<int>::of(&QComboBox::currentIndexChanged),this, [comboLang, this](int){
        emit languageChanged(comboLang->currentData().toString());
    });

}
