#include "hwconfigwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

#include "core/power/powercontrol.h"
#include "core/cx2.h"
#include "core/emu.h"
#include "core/misc.h"
#include "core/memory/mem.h"

namespace {
constexpr int kBatteryMvMin = 3000;
constexpr int kBatteryMvMax = 4200;
constexpr int kRailMvMin = 0;
constexpr int kRailMvMax = 5500;

int battery_mv_from_legacy_raw(int raw)
{
    if (raw < 0)
        return -1;
    if (raw > 930)
        raw = 930;
    return kBatteryMvMin + (raw * (kBatteryMvMax - kBatteryMvMin) + 465) / 930;
}

int legacy_raw_from_battery_mv(int mv)
{
    if (mv < kBatteryMvMin)
        mv = kBatteryMvMin;
    if (mv > kBatteryMvMax)
        mv = kBatteryMvMax;
    return ((mv - kBatteryMvMin) * 930 + (kBatteryMvMax - kBatteryMvMin) / 2)
            / (kBatteryMvMax - kBatteryMvMin);
}
}

HwConfigWidget::HwConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    /* -- Hardware Info --------------------------------------- */
    auto *infoGroup = new QGroupBox(tr("Hardware Info"), this);
    auto *infoLayout = new QFormLayout(infoGroup);
    m_productLabel = new QLabel(QStringLiteral("--"), infoGroup);
    m_flashSizeLabel = new QLabel(QStringLiteral("--"), infoGroup);
    infoLayout->addRow(tr("Product:"), m_productLabel);
    infoLayout->addRow(tr("Flash:"), m_flashSizeLabel);
    layout->addWidget(infoGroup);

    /* -- Power State ----------------------------------------- */
    auto *powerGroup = new QGroupBox(tr("Power State"), this);
    auto *powerLayout = new QFormLayout(powerGroup);
    m_usbSourceCombo = new QComboBox(powerGroup);
    m_usbSourceCombo->addItem(tr("Disconnected"), static_cast<int>(PowerControl::UsbPowerSource::Disconnected));
    m_usbSourceCombo->addItem(tr("Computer (data)"), static_cast<int>(PowerControl::UsbPowerSource::Computer));
    m_usbSourceCombo->addItem(tr("Charger (power only)"), static_cast<int>(PowerControl::UsbPowerSource::Charger));
    m_usbSourceCombo->addItem(tr("OTG cable (host-id)"), static_cast<int>(PowerControl::UsbPowerSource::OtgCable));
    m_batteryPresentCheck = new QCheckBox(tr("Battery inserted"), powerGroup);
    m_batteryPresentCheck->setChecked(true);
    m_dockPresentCheck = new QCheckBox(tr("Dock attached"), powerGroup);
    m_dockPresentCheck->setChecked(false);
    m_vbusSlider = new QSlider(Qt::Horizontal, powerGroup);
    m_vbusSlider->setRange(kRailMvMin, kRailMvMax);
    m_vbusSlider->setValue(5000);
    m_vbusInputLabel = new QLabel(QStringLiteral("5000 mV"), powerGroup);
    m_vbusInputLabel->setMinimumWidth(72);
    m_vsledSlider = new QSlider(Qt::Horizontal, powerGroup);
    m_vsledSlider->setRange(kRailMvMin, kRailMvMax);
    m_vsledSlider->setValue(0);
    m_vsledInputLabel = new QLabel(QStringLiteral("0 mV"), powerGroup);
    m_vsledInputLabel->setMinimumWidth(72);
    m_backResetButton = new QPushButton(tr("Press Back Reset"), powerGroup);
    m_batteryRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_vsysRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_vsledRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_vbusRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_vrefRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_vrefAuxRailLabel = new QLabel(QStringLiteral("--"), powerGroup);
    m_chargeStateLabel = new QLabel(QStringLiteral("--"), powerGroup);
    powerLayout->addRow(tr("USB source:"), m_usbSourceCombo);
    powerLayout->addRow(m_batteryPresentCheck);
    powerLayout->addRow(m_dockPresentCheck);
    {
        auto *row = new QHBoxLayout;
        row->addWidget(m_vbusSlider, 1);
        row->addWidget(m_vbusInputLabel);
        powerLayout->addRow(tr("VBUS input:"), row);
    }
    {
        auto *row = new QHBoxLayout;
        row->addWidget(m_vsledSlider, 1);
        row->addWidget(m_vsledInputLabel);
        powerLayout->addRow(tr("VSLED input:"), row);
    }
    powerLayout->addRow(m_backResetButton);
    powerLayout->addRow(tr("Charge state:"), m_chargeStateLabel);
    powerLayout->addRow(tr("VBAT:"), m_batteryRailLabel);
    powerLayout->addRow(tr("VSYS:"), m_vsysRailLabel);
    powerLayout->addRow(tr("VSLED:"), m_vsledRailLabel);
    powerLayout->addRow(tr("VBUS:"), m_vbusRailLabel);
    powerLayout->addRow(tr("VREF:"), m_vrefRailLabel);
    powerLayout->addRow(tr("VREF2:"), m_vrefAuxRailLabel);
    layout->addWidget(powerGroup);

    connect(m_usbSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const int data = m_usbSourceCombo->currentData().toInt();
        PowerControl::setUsbPowerSource(static_cast<PowerControl::UsbPowerSource>(data));
        const bool usbPowered = data == static_cast<int>(PowerControl::UsbPowerSource::Computer)
            || data == static_cast<int>(PowerControl::UsbPowerSource::Charger);
        m_vbusSlider->setEnabled(usbPowered);
        if (!usbPowered) {
            m_vbusSlider->blockSignals(true);
            m_vbusSlider->setValue(0);
            m_vbusSlider->blockSignals(false);
            m_vbusInputLabel->setText(QStringLiteral("0 mV"));
        } else if (m_vbusSlider->value() < 4500) {
            m_vbusSlider->blockSignals(true);
            m_vbusSlider->setValue(5000);
            m_vbusSlider->blockSignals(false);
            m_vbusInputLabel->setText(QStringLiteral("5000 mV"));
        }
        applyExternalRailOverrides();
        updatePowerRailsReadout();
    });
    connect(m_batteryPresentCheck, &QCheckBox::toggled, this, [this](bool on) {
        PowerControl::setBatteryPresent(on);
        m_batteryOverride->setEnabled(on);
        m_batterySlider->setEnabled(on && m_batteryOverride->isChecked());
        applyBatteryOverride();
        updatePowerRailsReadout();
    });
    connect(m_dockPresentCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_vsledSlider->setEnabled(on);
        if (!on) {
            m_vsledSlider->blockSignals(true);
            m_vsledSlider->setValue(0);
            m_vsledSlider->blockSignals(false);
            m_vsledInputLabel->setText(QStringLiteral("0 mV"));
        } else if (m_vsledSlider->value() < 4500) {
            m_vsledSlider->blockSignals(true);
            m_vsledSlider->setValue(5000);
            m_vsledSlider->blockSignals(false);
            m_vsledInputLabel->setText(QStringLiteral("5000 mV"));
        }
        applyExternalRailOverrides();
        updatePowerRailsReadout();
    });
    connect(m_vbusSlider, &QSlider::valueChanged, this, [this](int v) {
        m_vbusInputLabel->setText(QStringLiteral("%1 mV").arg(v));
        applyExternalRailOverrides();
        updatePowerRailsReadout();
    });
    connect(m_vsledSlider, &QSlider::valueChanged, this, [this](int v) {
        m_vsledInputLabel->setText(QStringLiteral("%1 mV").arg(v));
        applyExternalRailOverrides();
        updatePowerRailsReadout();
    });
    connect(m_backResetButton, &QPushButton::clicked, this, []() {
        PowerControl::pressBackResetButton();
    });

    /* -- Battery -------------------------------------------- */
    auto *batteryGroup = new QGroupBox(tr("Battery"), this);
    auto *batteryLayout = new QVBoxLayout(batteryGroup);

    m_batteryOverride = new QCheckBox(tr("Override"), batteryGroup);
    batteryLayout->addWidget(m_batteryOverride);

    auto *batteryRow = new QHBoxLayout;
    m_batterySlider = new QSlider(Qt::Horizontal, batteryGroup);
    m_batterySlider->setRange(kBatteryMvMin, kBatteryMvMax);
    m_batterySlider->setValue(4000);
    m_batterySlider->setEnabled(false);
    m_batteryLabel = new QLabel(QStringLiteral("4000 mV"), batteryGroup);
    m_batteryLabel->setMinimumWidth(72);
    batteryRow->addWidget(m_batterySlider, 1);
    batteryRow->addWidget(m_batteryLabel);
    batteryLayout->addLayout(batteryRow);

    layout->addWidget(batteryGroup);

    connect(m_batteryOverride, &QCheckBox::toggled, this, [this](bool on) {
        const bool batteryPresent = PowerControl::isBatteryPresent();
        m_batterySlider->setEnabled(on && batteryPresent);
        applyBatteryOverride();
    });
    connect(m_batterySlider, &QSlider::valueChanged, this, [this](int v) {
        m_batteryLabel->setText(QStringLiteral("%1 mV").arg(v));
        applyBatteryOverride();
    });
    /* -- Display Contrast ----------------------------------- */
    auto *displayGroup = new QGroupBox(tr("Display Contrast"), this);
    auto *displayLayout = new QVBoxLayout(displayGroup);

    m_contrastOverride = new QCheckBox(tr("Override"), displayGroup);
    displayLayout->addWidget(m_contrastOverride);

    auto *contrastRow = new QHBoxLayout;
    m_contrastSlider = new QSlider(Qt::Horizontal, displayGroup);
    m_contrastSlider->setRange(0, LCD_CONTRAST_MAX);
    m_contrastSlider->setValue(LCD_CONTRAST_MAX);
    m_contrastSlider->setEnabled(false);
    m_contrastLabel = new QLabel(QString::number(LCD_CONTRAST_MAX), displayGroup);
    m_contrastLabel->setMinimumWidth(96);
    contrastRow->addWidget(m_contrastSlider, 1);
    contrastRow->addWidget(m_contrastLabel);
    displayLayout->addLayout(contrastRow);
    layout->addWidget(displayGroup);

    connect(m_contrastOverride, &QCheckBox::toggled, this, [this](bool on) {
        m_contrastSlider->setEnabled(on);
        applyContrastOverride();
    });
    connect(m_contrastSlider, &QSlider::valueChanged, this, [this](int v) {
        setContrastLabelForValues(v, contrastFromSliderValue(v));
        applyContrastOverride();
    });

    /* -- Keypad Type ---------------------------------------- */
    auto *keypadGroup = new QGroupBox(tr("Keypad Type"), this);
    auto *keypadLayout = new QFormLayout(keypadGroup);
    m_keypadTypeCombo = new QComboBox(keypadGroup);
    m_keypadTypeCombo->addItem(tr("Touchpad"), 73);
    m_keypadTypeCombo->addItem(tr("Classic Clickpad"), 10);
    m_keypadTypeCombo->addItem(tr("TI-84+ Keypad"), 30);
    m_keypadTypeCombo->addItem(tr("Default (auto)"), -1);
    m_keypadTypeCombo->setCurrentIndex(3); /* Default */
    keypadLayout->addRow(tr("Type:"), m_keypadTypeCombo);
    layout->addWidget(keypadGroup);

    connect(m_keypadTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyKeypadType(); });

    layout->addStretch(1);

    /* Poll contrast from the emulated OS while the widget is visible */
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &HwConfigWidget::pollContrast);

    updateContrastSliderMode();
    updateKeypadTypeChoices();
    syncOverridesFromGlobals();
}

void HwConfigWidget::refresh()
{
    /* Hardware info (read-only) */
    QString prodStr;
    if (product >= 0x1C0)
        prodStr = QStringLiteral("0x%1 (CX II)").arg(product, 3, 16, QLatin1Char('0'));
    else if (product >= 0x0F0)
        prodStr = QStringLiteral("0x%1 (CX)").arg(product, 3, 16, QLatin1Char('0'));
    else
        prodStr = QStringLiteral("0x%1 (Classic)").arg(product, 3, 16, QLatin1Char('0'));
    if (product >= 0x0F0)
        prodStr += QStringLiteral("  Features: %1").arg((features & 1u) ? QStringLiteral("CAS")
                                                                         : QStringLiteral("non-CAS"));
    m_productLabel->setText(prodStr);

    uint32_t ram_size = mem_areas[1].size;
    m_flashSizeLabel->setText(QStringLiteral("%1 MB RAM").arg(ram_size / (1024 * 1024)));

    updateContrastSliderMode();
    updateKeypadTypeChoices();

    /* Read current contrast */
    if (!m_contrastOverride->isChecked()) {
        int contrast = hdq1w.lcd_contrast;
        int sliderValue = sliderValueFromContrast(contrast);
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(sliderValue);
        m_contrastSlider->blockSignals(false);
        setContrastLabelForValues(sliderValue, contrast);
    }

    const int sourceData = static_cast<int>(PowerControl::usbPowerSource());
    const int sourceIndex = m_usbSourceCombo->findData(sourceData);
    m_usbSourceCombo->blockSignals(true);
    if (sourceIndex >= 0)
        m_usbSourceCombo->setCurrentIndex(sourceIndex);
    m_usbSourceCombo->blockSignals(false);
    m_batteryPresentCheck->blockSignals(true);
    m_batteryPresentCheck->setChecked(PowerControl::isBatteryPresent());
    m_batteryPresentCheck->blockSignals(false);
    m_dockPresentCheck->blockSignals(true);
    m_dockPresentCheck->setChecked(PowerControl::isDockAttached());
    m_dockPresentCheck->blockSignals(false);
    m_vbusSlider->blockSignals(true);
    m_vbusSlider->setValue(PowerControl::usbBusMillivolts());
    m_vbusSlider->blockSignals(false);
    m_vbusInputLabel->setText(QStringLiteral("%1 mV").arg(m_vbusSlider->value()));
    m_vbusSlider->setEnabled(sourceData == static_cast<int>(PowerControl::UsbPowerSource::Computer)
        || sourceData == static_cast<int>(PowerControl::UsbPowerSource::Charger));
    m_vsledSlider->blockSignals(true);
    m_vsledSlider->setValue(PowerControl::dockRailMillivolts());
    m_vsledSlider->blockSignals(false);
    m_vsledInputLabel->setText(QStringLiteral("%1 mV").arg(m_vsledSlider->value()));
    m_vsledSlider->setEnabled(m_dockPresentCheck->isChecked());
    updatePowerRailsReadout();
}

void HwConfigWidget::syncOverridesFromGlobals()
{
    const int16_t savedBatteryRaw = hw_override_get_adc_battery_level();
    const int savedBatteryMvOverride = hw_override_get_battery_mv();
    int savedBatteryMv = -1;
    if (savedBatteryMvOverride >= 0) {
        savedBatteryMv = savedBatteryMvOverride;
    } else if (!emulate_cx2) {
        savedBatteryMv = battery_mv_from_legacy_raw(savedBatteryRaw);
    }
    if (savedBatteryMv < 0)
        savedBatteryMv = 4000;

    const int16_t savedContrast = hw_override_get_lcd_contrast();
    const int16_t savedKeypad   = hw_override_get_adc_keypad_type();
    const bool batteryPresent = PowerControl::isBatteryPresent();
    const bool dockPresent = PowerControl::isDockAttached();
    const int vbusMv = PowerControl::usbBusMillivolts();
    const int vsledMv = PowerControl::dockRailMillivolts();
    const int usbSourceData = static_cast<int>(PowerControl::usbPowerSource());

    m_batteryPresentCheck->blockSignals(true);
    m_batteryPresentCheck->setChecked(batteryPresent);
    m_batteryPresentCheck->blockSignals(false);
    m_dockPresentCheck->blockSignals(true);
    m_dockPresentCheck->setChecked(dockPresent);
    m_dockPresentCheck->blockSignals(false);
    int usbSourceIndex = m_usbSourceCombo->findData(usbSourceData);
    if (usbSourceIndex < 0)
        usbSourceIndex = 0;
    m_usbSourceCombo->blockSignals(true);
    m_usbSourceCombo->setCurrentIndex(usbSourceIndex);
    m_usbSourceCombo->blockSignals(false);
    m_vbusSlider->blockSignals(true);
    m_vbusSlider->setValue(vbusMv);
    m_vbusSlider->blockSignals(false);
    m_vbusInputLabel->setText(QStringLiteral("%1 mV").arg(vbusMv));
    m_vbusSlider->setEnabled(usbSourceData == static_cast<int>(PowerControl::UsbPowerSource::Computer)
        || usbSourceData == static_cast<int>(PowerControl::UsbPowerSource::Charger));
    m_vsledSlider->blockSignals(true);
    m_vsledSlider->setValue(vsledMv);
    m_vsledSlider->blockSignals(false);
    m_vsledInputLabel->setText(QStringLiteral("%1 mV").arg(vsledMv));
    m_vsledSlider->setEnabled(dockPresent);

    m_batterySlider->blockSignals(true);
    m_batterySlider->setValue(savedBatteryMv);
    m_batterySlider->blockSignals(false);
    m_batteryLabel->setText(QStringLiteral("%1 mV").arg(savedBatteryMv));

    bool batteryOn = (savedBatteryMvOverride >= 0)
        || (!emulate_cx2 && savedBatteryRaw >= 0);
    m_batteryOverride->blockSignals(true);
    m_batteryOverride->setChecked(batteryOn);
    m_batteryOverride->blockSignals(false);
    m_batteryOverride->setEnabled(batteryPresent);
    m_batterySlider->setEnabled(batteryOn && batteryPresent);
    applyBatteryOverride();
    applyExternalRailOverrides();
    updatePowerRailsReadout();

    updateContrastSliderMode();
    int contrast = (savedContrast >= 0) ? savedContrast : hdq1w.lcd_contrast;
    int sliderValue = sliderValueFromContrast(contrast);
    m_contrastSlider->blockSignals(true);
    m_contrastSlider->setValue(sliderValue);
    m_contrastSlider->blockSignals(false);
    setContrastLabelForValues(sliderValue, contrast);
    bool contrastOn = savedContrast >= 0;
    m_contrastOverride->blockSignals(true);
    m_contrastOverride->setChecked(contrastOn);
    m_contrastOverride->blockSignals(false);
    m_contrastSlider->setEnabled(contrastOn);
    applyContrastOverride();

    updateKeypadTypeChoices();
    if (emulate_cx2) {
        m_keypadTypeCombo->setCurrentIndex(0);
    } else if (savedKeypad >= 0) {
        int idx = m_keypadTypeCombo->findData((int)savedKeypad);
        if (idx >= 0)
            m_keypadTypeCombo->setCurrentIndex(idx);
    } else {
        int idx = m_keypadTypeCombo->findData(-1);
        if (idx >= 0)
            m_keypadTypeCombo->setCurrentIndex(idx);
    }
    applyKeypadType();
}

void HwConfigWidget::applyBatteryOverride()
{
    if (!PowerControl::isBatteryPresent()) {
        hw_override_set_battery_mv(-1);
        hw_override_set_adc_battery_level(-1);
        hw_override_set_adc_charging(-1);
        hw_override_set_charger_state(CHARGER_AUTO);
        m_batterySlider->setEnabled(false);
        const int sourceData = static_cast<int>(PowerControl::usbPowerSource());
        const int sourceIndex = m_usbSourceCombo->findData(sourceData);
        m_usbSourceCombo->blockSignals(true);
        if (sourceIndex >= 0)
            m_usbSourceCombo->setCurrentIndex(sourceIndex);
        m_usbSourceCombo->blockSignals(false);
        PowerControl::refreshPowerState();
        updatePowerRailsReadout();
        return;
    }

    if (m_batteryOverride->isChecked()) {
        int mv = m_batterySlider->value();
        hw_override_set_battery_mv(mv);
        hw_override_set_adc_battery_level((int16_t)legacy_raw_from_battery_mv(mv));
    } else {
        hw_override_set_battery_mv(-1);
        hw_override_set_adc_battery_level(-1);
    }
    hw_override_set_adc_charging(-1);
    hw_override_set_charger_state(CHARGER_AUTO);
    const int sourceData = static_cast<int>(PowerControl::usbPowerSource());
    const int sourceIndex = m_usbSourceCombo->findData(sourceData);
    m_usbSourceCombo->blockSignals(true);
    if (sourceIndex >= 0)
        m_usbSourceCombo->setCurrentIndex(sourceIndex);
    m_usbSourceCombo->blockSignals(false);
    PowerControl::refreshPowerState();
    updatePowerRailsReadout();
}

void HwConfigWidget::applyExternalRailOverrides()
{
    PowerControl::setDockAttached(m_dockPresentCheck->isChecked());
    PowerControl::setUsbBusMillivolts(m_vbusSlider->value());
    PowerControl::setDockRailMillivolts(m_vsledSlider->value());
    PowerControl::refreshPowerState();
}

void HwConfigWidget::applyContrastOverride()
{
    if (m_contrastOverride->isChecked()) {
        const int contrast = contrastFromSliderValue(m_contrastSlider->value());
        hw_override_set_lcd_contrast((int16_t)contrast);
        /* Apply immediately to the hdq1w register */
        hdq1w.lcd_contrast = (uint8_t)contrast;
    } else {
        hw_override_set_lcd_contrast(-1);
        if (emulate_cx2)
            cx2_backlight_refresh_lcd_contrast();
    }
}

void HwConfigWidget::applyKeypadType()
{
    if (emulate_cx2) {
        hw_override_set_adc_keypad_type(73);
        return;
    }
    int val = m_keypadTypeCombo->currentData().toInt();
    hw_override_set_adc_keypad_type((int16_t)val);
}

void HwConfigWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_pollTimer->start();
}

void HwConfigWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pollTimer->stop();
}

void HwConfigWidget::pollContrast()
{
    if (m_contrastOverride->isChecked())
        return;

    int contrast = hdq1w.lcd_contrast;
    int sliderValue = sliderValueFromContrast(contrast);
    if (m_contrastSlider->value() != sliderValue) {
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(sliderValue);
        m_contrastSlider->blockSignals(false);
        setContrastLabelForValues(sliderValue, contrast);
    }

    updatePowerRailsReadout();
}
