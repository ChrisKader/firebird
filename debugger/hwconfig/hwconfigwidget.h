#ifndef HWCONFIGWIDGET_H
#define HWCONFIGWIDGET_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QTimer>

class HwConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HwConfigWidget(QWidget *parent = nullptr);

public slots:
    void refresh();
    void syncOverridesFromGlobals();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void applyBatteryOverride();
    void applyContrastOverride();
    void applyKeypadType();
    void pollContrast();

    /* Hardware Info (read-only) */
    QLabel *m_productLabel;
    QLabel *m_flashSizeLabel;

    /* Battery section */
    QSlider *m_batterySlider;
    QLabel *m_batteryLabel;
    QComboBox *m_chargerStateCombo;
    QCheckBox *m_batteryOverride;

    /* Display section */
    QSlider *m_contrastSlider;
    QLabel *m_contrastLabel;
    QCheckBox *m_contrastOverride;

    /* Keypad type section */
    QComboBox *m_keypadTypeCombo;

    /* Periodic poll for live contrast readout */
    QTimer *m_pollTimer = nullptr;
};

#endif // HWCONFIGWIDGET_H
