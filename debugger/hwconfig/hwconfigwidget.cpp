#include "hwconfigwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

#include "core/emu.h"
#include "core/misc.h"
#include "core/mem.h"

namespace {
constexpr int kBatteryMvMin = 3000;
constexpr int kBatteryMvMax = 4200;

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

charger_state_t charging_state_from_legacy(int8_t value)
{
    if (value > 0)
        return CHARGER_CHARGING;
    return CHARGER_DISCONNECTED;
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

    m_chargerStateCombo = new QComboBox(batteryGroup);
    m_chargerStateCombo->addItem(tr("Disconnected"), (int)CHARGER_DISCONNECTED);
    m_chargerStateCombo->addItem(tr("Connected (idle)"), (int)CHARGER_CONNECTED_NOT_CHARGING);
    m_chargerStateCombo->addItem(tr("Charging"), (int)CHARGER_CHARGING);
    m_chargerStateCombo->setEnabled(false);
    batteryLayout->addWidget(m_chargerStateCombo);
    layout->addWidget(batteryGroup);

    connect(m_batteryOverride, &QCheckBox::toggled, this, [this](bool on) {
        m_batterySlider->setEnabled(on);
        m_chargerStateCombo->setEnabled(on);
        applyBatteryOverride();
    });
    connect(m_batterySlider, &QSlider::valueChanged, this, [this](int v) {
        m_batteryLabel->setText(QStringLiteral("%1 mV").arg(v));
        applyBatteryOverride();
    });
    connect(m_chargerStateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
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
    m_contrastLabel->setMinimumWidth(40);
    contrastRow->addWidget(m_contrastSlider, 1);
    contrastRow->addWidget(m_contrastLabel);
    displayLayout->addLayout(contrastRow);
    layout->addWidget(displayGroup);

    connect(m_contrastOverride, &QCheckBox::toggled, this, [this](bool on) {
        m_contrastSlider->setEnabled(on);
        applyContrastOverride();
    });
    connect(m_contrastSlider, &QSlider::valueChanged, this, [this](int v) {
        m_contrastLabel->setText(QString::number(v));
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
    m_productLabel->setText(prodStr);

    uint32_t ram_size = mem_areas[1].size;
    m_flashSizeLabel->setText(QStringLiteral("%1 MB RAM").arg(ram_size / (1024 * 1024)));

    /* Read current contrast */
    if (!m_contrastOverride->isChecked()) {
        int contrast = hdq1w.lcd_contrast;
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(contrast);
        m_contrastSlider->blockSignals(false);
        m_contrastLabel->setText(QString::number(contrast));
    }
}

void HwConfigWidget::syncOverridesFromGlobals()
{
    const int16_t savedBatteryRaw = adc_battery_level_override;
    int savedBatteryMv = (battery_mv_override >= 0)
        ? battery_mv_override : battery_mv_from_legacy_raw(savedBatteryRaw);
    if (savedBatteryMv < 0)
        savedBatteryMv = 4000;

    charger_state_t savedCharging = (charger_state_override >= CHARGER_DISCONNECTED
        && charger_state_override <= CHARGER_CHARGING)
        ? charger_state_override : charging_state_from_legacy(adc_charging_override);
    const int16_t savedContrast = lcd_contrast_override;
    const int16_t savedKeypad   = adc_keypad_type_override;

    m_batterySlider->blockSignals(true);
    m_batterySlider->setValue(savedBatteryMv);
    m_batterySlider->blockSignals(false);
    m_batteryLabel->setText(QStringLiteral("%1 mV").arg(savedBatteryMv));

    int stateIndex = m_chargerStateCombo->findData((int)savedCharging);
    if (stateIndex < 0)
        stateIndex = 0;
    m_chargerStateCombo->blockSignals(true);
    m_chargerStateCombo->setCurrentIndex(stateIndex);
    m_chargerStateCombo->blockSignals(false);

    bool batteryOn = (battery_mv_override >= 0 || adc_battery_level_override >= 0);
    m_batteryOverride->blockSignals(true);
    m_batteryOverride->setChecked(batteryOn);
    m_batteryOverride->blockSignals(false);
    m_batterySlider->setEnabled(batteryOn);
    m_chargerStateCombo->setEnabled(batteryOn);
    applyBatteryOverride();

    int contrast = (savedContrast >= 0) ? savedContrast : LCD_CONTRAST_MAX;
    m_contrastSlider->blockSignals(true);
    m_contrastSlider->setValue(contrast);
    m_contrastSlider->blockSignals(false);
    m_contrastLabel->setText(QString::number(contrast));
    bool contrastOn = savedContrast >= 0;
    m_contrastOverride->blockSignals(true);
    m_contrastOverride->setChecked(contrastOn);
    m_contrastOverride->blockSignals(false);
    m_contrastSlider->setEnabled(contrastOn);
    applyContrastOverride();

    if (savedKeypad >= 0) {
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
    if (m_batteryOverride->isChecked()) {
        int mv = m_batterySlider->value();
        int stateData = m_chargerStateCombo->currentData().toInt();
        if (stateData < CHARGER_DISCONNECTED || stateData > CHARGER_CHARGING)
            stateData = CHARGER_DISCONNECTED;
        battery_mv_override = mv;
        adc_battery_level_override = (int16_t)legacy_raw_from_battery_mv(mv);
        charger_state_override = (charger_state_t)stateData;
        adc_charging_override = (charger_state_override == CHARGER_CHARGING) ? 1 : 0;
    } else {
        battery_mv_override = -1;
        adc_battery_level_override = -1;
        adc_charging_override = -1;
        charger_state_override = CHARGER_DISCONNECTED;
    }
}

void HwConfigWidget::applyContrastOverride()
{
    if (m_contrastOverride->isChecked()) {
        lcd_contrast_override = (int16_t)m_contrastSlider->value();
        /* Apply immediately to the hdq1w register */
        hdq1w.lcd_contrast = (uint8_t)m_contrastSlider->value();
    } else {
        lcd_contrast_override = -1;
    }
}

void HwConfigWidget::applyKeypadType()
{
    int val = m_keypadTypeCombo->currentData().toInt();
    adc_keypad_type_override = (int16_t)val;
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
    if (m_contrastSlider->value() != contrast) {
        m_contrastSlider->blockSignals(true);
        m_contrastSlider->setValue(contrast);
        m_contrastSlider->blockSignals(false);
        m_contrastLabel->setText(QString::number(contrast));
    }
}
