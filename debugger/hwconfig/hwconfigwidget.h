#ifndef HWCONFIGWIDGET_H
#define HWCONFIGWIDGET_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>
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
    void updateKeypadTypeChoices();
    void updateContrastSliderMode();
    int sliderValueFromContrast(int contrast) const;
    int contrastFromSliderValue(int sliderValue) const;
    void setContrastLabelForValues(int sliderValue, int contrast);

    void applyBatteryOverride();
    void applyExternalRailOverrides();
    void applyContrastOverride();
    void applyKeypadType();
    void pollContrast();
    void updatePowerRailsReadout();

    /* Hardware Info (read-only) */
    QLabel *m_productLabel;
    QLabel *m_flashSizeLabel;

    /* Battery section */
    QSlider *m_batterySlider;
    QLabel *m_batteryLabel;
    QCheckBox *m_batteryOverride;

    /* Display section */
    QSlider *m_contrastSlider;
    QLabel *m_contrastLabel;
    QCheckBox *m_contrastOverride;

    /* Keypad type section */
    QComboBox *m_keypadTypeCombo;

    /* Power controls */
    QComboBox *m_usbSourceCombo = nullptr;
    QCheckBox *m_batteryPresentCheck = nullptr;
    QCheckBox *m_dockPresentCheck = nullptr;
    QPushButton *m_backResetButton = nullptr;
    QSlider *m_vbusSlider = nullptr;
    QLabel *m_vbusInputLabel = nullptr;
    QSlider *m_vsledSlider = nullptr;
    QLabel *m_vsledInputLabel = nullptr;
    QLabel *m_batteryRailLabel = nullptr;
    QLabel *m_vsysRailLabel = nullptr;
    QLabel *m_vsledRailLabel = nullptr;
    QLabel *m_vbusRailLabel = nullptr;
    QLabel *m_vrefRailLabel = nullptr;
    QLabel *m_vrefAuxRailLabel = nullptr;
    QLabel *m_chargeStateLabel = nullptr;

    /* Periodic poll for live contrast readout */
    QTimer *m_pollTimer = nullptr;
};

#endif // HWCONFIGWIDGET_H
