#ifndef CONTROLPANELDIALOG_H
#define CONTROLPANELDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QFontComboBox>
#include <QSpinBox>

class ControlPanelDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ControlPanelDialog(QWidget *parent = nullptr);

signals:
    void seekRelative(double offset);
    void setPlaybackSpeed(double speed);
    void subtitleFontChanged(const QFont &font);
    void subtitleFontSizeChanged(int size);
    void languageChanged(const QString &locale);
};

#endif // CONTROLPANELDIALOG_H
