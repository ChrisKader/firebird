#include "hwconfigwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

#include "core/emu.h"
#include "core/misc.h"
#include "core/mem.h"

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
    m_batterySlider->setRange(0, 100);
    m_batterySlider->setValue(100);
    m_batterySlider->setEnabled(false);
    m_batteryLabel = new QLabel(QStringLiteral("100%"), batteryGroup);
    m_batteryLabel->setMinimumWidth(40);
    batteryRow->addWidget(m_batterySlider, 1);
    batteryRow->addWidget(m_batteryLabel);
    batteryLayout->addLayout(batteryRow);

    m_chargingBox = new QCheckBox(tr("Charging"), batteryGroup);
    m_chargingBox->setEnabled(false);
    batteryLayout->addWidget(m_chargingBox);
    layout->addWidget(batteryGroup);

    connect(m_batteryOverride, &QCheckBox::toggled, this, [this](bool on) {
        m_batterySlider->setEnabled(on);
        m_chargingBox->setEnabled(on);
        applyBatteryOverride();
    });
    connect(m_batterySlider, &QSlider::valueChanged, this, [this](int v) {
        m_batteryLabel->setText(QStringLiteral("%1%").arg(v));
        applyBatteryOverride();
    });
    connect(m_chargingBox, &QCheckBox::toggled, this, [this]() {
        applyBatteryOverride();
    });

    /* -- Display -------------------------------------------- */
    auto *displayGroup = new QGroupBox(tr("Display"), this);
    auto *displayLayout = new QVBoxLayout(displayGroup);

    m_brightnessOverride = new QCheckBox(tr("Override"), displayGroup);
    displayLayout->addWidget(m_brightnessOverride);

    auto *brightnessRow = new QHBoxLayout;
    m_brightnessSlider = new QSlider(Qt::Horizontal, displayGroup);
    m_brightnessSlider->setRange(0, 255);
    m_brightnessSlider->setValue(128);
    m_brightnessSlider->setEnabled(false);
    m_brightnessLabel = new QLabel(QStringLiteral("128"), displayGroup);
    m_brightnessLabel->setMinimumWidth(40);
    brightnessRow->addWidget(m_brightnessSlider, 1);
    brightnessRow->addWidget(m_brightnessLabel);
    displayLayout->addLayout(brightnessRow);
    layout->addWidget(displayGroup);

    connect(m_brightnessOverride, &QCheckBox::toggled, this, [this](bool on) {
        m_brightnessSlider->setEnabled(on);
        applyBrightnessOverride();
    });
    connect(m_brightnessSlider, &QSlider::valueChanged, this, [this](int v) {
        m_brightnessLabel->setText(QString::number(v));
        applyBrightnessOverride();
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

    /* Read current brightness */
    if (!m_brightnessOverride->isChecked()) {
        int contrast = hdq1w.lcd_contrast;
        m_brightnessSlider->blockSignals(true);
        m_brightnessSlider->setValue(contrast);
        m_brightnessSlider->blockSignals(false);
        m_brightnessLabel->setText(QString::number(contrast));
    }
}

void HwConfigWidget::applyBatteryOverride()
{
    if (m_batteryOverride->isChecked()) {
        int pct = m_batterySlider->value();
        adc_battery_level_override = (int16_t)(pct * 930 / 100);
        adc_charging_override = m_chargingBox->isChecked() ? 1 : 0;
    } else {
        adc_battery_level_override = -1;
        adc_charging_override = -1;
    }
}

void HwConfigWidget::applyBrightnessOverride()
{
    if (m_brightnessOverride->isChecked()) {
        lcd_brightness_override = (int16_t)m_brightnessSlider->value();
        /* Apply immediately to the hdq1w register */
        hdq1w.lcd_contrast = (uint8_t)m_brightnessSlider->value();
    } else {
        lcd_brightness_override = -1;
    }
}

void HwConfigWidget::applyKeypadType()
{
    int val = m_keypadTypeCombo->currentData().toInt();
    adc_keypad_type_override = (int16_t)val;
}
