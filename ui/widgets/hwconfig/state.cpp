#include "hwconfigwidget.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "core/soc/cx2.h"
#include "core/emu.h"
#include "core/peripherals/misc.h"

namespace {
constexpr int kCx2BrightnessMinStep = -6;
constexpr int kCx2BrightnessMaxStep = 3;
constexpr int kCx2BrightnessDarkPwm = 0xF3;
constexpr int kCx2BrightnessStepPwm = 0x0F;

int cx2_contrast_from_step(int step)
{
    if (step < kCx2BrightnessMinStep)
        step = kCx2BrightnessMinStep;
    if (step > kCx2BrightnessMaxStep)
        step = kCx2BrightnessMaxStep;

    const int pwm = kCx2BrightnessDarkPwm - (step - kCx2BrightnessMinStep) * kCx2BrightnessStepPwm;
    int contrast = LCD_CONTRAST_MAX - (pwm * LCD_CONTRAST_MAX) / 255;
    if (contrast < 0)
        contrast = 0;
    if (contrast > LCD_CONTRAST_MAX)
        contrast = LCD_CONTRAST_MAX;
    return contrast;
}

QString format_rail_with_code(int mv, uint16_t code)
{
    return QStringLiteral("%1 mV (0x%2)")
        .arg(mv)
        .arg(code, 3, 16, QLatin1Char('0'));
}

QString charger_state_to_text(charger_state_t state)
{
    switch (state) {
    case CHARGER_CHARGING:
        return QStringLiteral("Charging");
    case CHARGER_CONNECTED_NOT_CHARGING:
        return QStringLiteral("Connected, idle");
    case CHARGER_DISCONNECTED:
        return QStringLiteral("Disconnected");
    default:
        return QStringLiteral("Auto");
    }
}
} // namespace

void HwConfigWidget::updatePowerRailsReadout()
{
    if (!m_chargeStateLabel)
        return;

    if (!emulate_cx2) {
        m_chargeStateLabel->setText(QStringLiteral("n/a"));
        m_batteryRailLabel->setText(QStringLiteral("n/a"));
        m_vsysRailLabel->setText(QStringLiteral("n/a"));
        m_vsledRailLabel->setText(QStringLiteral("n/a"));
        m_vbusRailLabel->setText(QStringLiteral("n/a"));
        m_vrefRailLabel->setText(QStringLiteral("n/a"));
        m_vrefAuxRailLabel->setText(QStringLiteral("n/a"));
        return;
    }

    cx2_power_rails_t rails{};
    cx2_get_power_rails(&rails);
    m_chargeStateLabel->setText(charger_state_to_text(rails.charger_state));
    m_batteryRailLabel->setText(
        rails.battery_present
            ? format_rail_with_code(rails.battery_mv, rails.battery_code)
            : QStringLiteral("absent (0x%1)").arg(rails.battery_code, 3, 16, QLatin1Char('0')));
    m_vsysRailLabel->setText(format_rail_with_code(rails.vsys_mv, rails.vsys_code));
    m_vsledRailLabel->setText(format_rail_with_code(rails.vsled_mv, rails.vsled_code));
    m_vbusRailLabel->setText(format_rail_with_code(rails.vbus_mv, rails.vbus_code));
    m_vrefRailLabel->setText(format_rail_with_code(rails.vref_mv, rails.vref_code));
    m_vrefAuxRailLabel->setText(format_rail_with_code(rails.vref_aux_mv, rails.vref_aux_code));
}

void HwConfigWidget::updateKeypadTypeChoices()
{
    const int current = m_keypadTypeCombo->currentData().toInt();
    m_keypadTypeCombo->blockSignals(true);
    m_keypadTypeCombo->clear();
    m_keypadTypeCombo->addItem(tr("Touchpad"), 73);
    if (emulate_cx2) {
        m_keypadTypeCombo->setEnabled(false);
        m_keypadTypeCombo->setCurrentIndex(0);
    } else {
        m_keypadTypeCombo->addItem(tr("Classic Clickpad"), 10);
        m_keypadTypeCombo->addItem(tr("TI-84+ Keypad"), 30);
        m_keypadTypeCombo->addItem(tr("Default (auto)"), -1);
        m_keypadTypeCombo->setEnabled(true);
        int idx = m_keypadTypeCombo->findData(current);
        if (idx < 0)
            idx = m_keypadTypeCombo->findData(-1);
        if (idx < 0)
            idx = 0;
        m_keypadTypeCombo->setCurrentIndex(idx);
    }
    m_keypadTypeCombo->blockSignals(false);
}

void HwConfigWidget::updateContrastSliderMode()
{
    const int value = m_contrastSlider->value();
    m_contrastSlider->blockSignals(true);
    if (emulate_cx2) {
        m_contrastSlider->setRange(kCx2BrightnessMinStep, kCx2BrightnessMaxStep);
        m_contrastSlider->setSingleStep(1);
        m_contrastSlider->setPageStep(1);
        int mapped = sliderValueFromContrast(value);
        m_contrastSlider->setValue(mapped);
    } else {
        m_contrastSlider->setRange(0, LCD_CONTRAST_MAX);
        m_contrastSlider->setSingleStep(1);
        m_contrastSlider->setPageStep(8);
        m_contrastSlider->setValue(std::clamp(value, 0, LCD_CONTRAST_MAX));
    }
    m_contrastSlider->blockSignals(false);
}

int HwConfigWidget::sliderValueFromContrast(int contrast) const
{
    if (!emulate_cx2)
        return std::clamp(contrast, 0, LCD_CONTRAST_MAX);

    int bestStep = kCx2BrightnessMinStep;
    int bestDiff = INT_MAX;
    for (int step = kCx2BrightnessMinStep; step <= kCx2BrightnessMaxStep; step++) {
        const int mapped = cx2_contrast_from_step(step);
        const int diff = std::abs(mapped - contrast);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestStep = step;
        }
    }
    return bestStep;
}

int HwConfigWidget::contrastFromSliderValue(int sliderValue) const
{
    if (!emulate_cx2)
        return std::clamp(sliderValue, 0, LCD_CONTRAST_MAX);
    return cx2_contrast_from_step(sliderValue);
}

void HwConfigWidget::setContrastLabelForValues(int sliderValue, int contrast)
{
    if (emulate_cx2)
        m_contrastLabel->setText(QStringLiteral("%1 (step %2)").arg(contrast).arg(sliderValue));
    else
        m_contrastLabel->setText(QString::number(contrast));
}
