#ifndef HWCONFIGWIDGET_H
#define HWCONFIGWIDGET_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <stdint.h>

class HwConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HwConfigWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void applyBatteryOverride();
    void applyBrightnessOverride();
    void applyKeypadType();

    /* Hardware Info (read-only) */
    QLabel *m_productLabel;
    QLabel *m_flashSizeLabel;

    /* Battery section */
    QSlider *m_batterySlider;
    QLabel *m_batteryLabel;
    QCheckBox *m_chargingBox;
    QCheckBox *m_batteryOverride;

    /* Display section */
    QSlider *m_brightnessSlider;
    QLabel *m_brightnessLabel;
    QCheckBox *m_brightnessOverride;

    /* Keypad type section */
    QComboBox *m_keypadTypeCombo;
};

#endif // HWCONFIGWIDGET_H
