#include "powercontrol.h"

#include <algorithm>

#include "core/usblink.h"
#include "core/usblink_queue.h"

namespace PowerControl {

static bool g_forcedOffNoPower = false;
static bool g_lastHasPowerInitialized = false;
static bool g_lastHasPower = false;

static constexpr int kMinBatteryMvForPower = 3300;
static constexpr int kMinExternalRailMvForPower = 4500;

int usbBusMillivolts();
bool isDockAttached();
int dockRailMillivolts();

static int effectiveBatteryMvForPower()
{
    const int batteryMv = hw_override_get_battery_mv();
    if (batteryMv >= 0)
        return batteryMv;

    const int batteryRaw = hw_override_get_adc_battery_level();
    if (batteryRaw >= 0) {
        const int clampedRaw = (batteryRaw > 930) ? 930 : batteryRaw;
        return 3000 + (clampedRaw * (4200 - 3000) + 465) / 930;
    }

    return 4200;
}

static bool usbSourceProvidesExternalPower(UsbPowerSource usbSource)
{
    return usbSource == UsbPowerSource::Computer
        || usbSource == UsbPowerSource::Charger;
}

static bool hasPower(bool batteryPresent, UsbPowerSource usbSource)
{
    const bool usbPower = usbSourceProvidesExternalPower(usbSource)
        && usbBusMillivolts() >= kMinExternalRailMvForPower;
    const bool dockPower = isDockAttached()
        && dockRailMillivolts() >= kMinExternalRailMvForPower;
    if (usbPower || dockPower)
        return true;
    return batteryPresent && (effectiveBatteryMvForPower() >= kMinBatteryMvForPower);
}

void refreshPowerState()
{
    const bool hasPowerNow = hasPower(isBatteryPresent(), usbPowerSource());
    if (!g_lastHasPowerInitialized) {
        g_lastHasPowerInitialized = true;
        g_lastHasPower = hasPowerNow;
        if (!hasPowerNow) {
            g_forcedOffNoPower = true;
            __atomic_fetch_or(&cpu_events, EVENT_SLEEP, __ATOMIC_RELAXED);
        }
        return;
    }

    if (!hasPowerNow) {
        g_forcedOffNoPower = true;
        __atomic_fetch_or(&cpu_events, EVENT_SLEEP, __ATOMIC_RELAXED);
    } else if (!g_lastHasPower || g_forcedOffNoPower) {
        g_forcedOffNoPower = false;
        __atomic_fetch_and(&cpu_events, ~(uint32_t)EVENT_SLEEP, __ATOMIC_RELAXED);
        emu_request_reset_hard();
    }
    g_lastHasPower = hasPowerNow;
}

UsbPowerSource usbPowerSource()
{
    if (hw_override_get_usb_otg_cable() > 0)
        return UsbPowerSource::OtgCable;
    if (hw_override_get_usb_cable_connected() <= 0)
        return UsbPowerSource::Disconnected;
    if (hw_override_get_vbus_mv() < kMinExternalRailMvForPower)
        return UsbPowerSource::Disconnected;
    if (usblink_connected || usblink_state != 0)
        return UsbPowerSource::Computer;
    return UsbPowerSource::Charger;
}

void setUsbPowerSource(UsbPowerSource source)
{
    switch (source) {
    case UsbPowerSource::Disconnected:
        hw_override_set_usb_otg_cable(0);
        hw_override_set_usb_cable_connected(0);
        hw_override_set_vbus_mv(0);
        usblink_queue_reset();
        usblink_reset();
        break;
    case UsbPowerSource::Computer:
        hw_override_set_usb_otg_cable(0);
        hw_override_set_usb_cable_connected(1);
        hw_override_set_vbus_mv(5000);
        usblink_connect();
        break;
    case UsbPowerSource::Charger:
        hw_override_set_usb_otg_cable(0);
        hw_override_set_usb_cable_connected(1);
        hw_override_set_vbus_mv(5000);
        usblink_queue_reset();
        usblink_reset();
        break;
    case UsbPowerSource::OtgCable:
        hw_override_set_usb_otg_cable(1);
        hw_override_set_usb_cable_connected(0);
        hw_override_set_vbus_mv(0);
        usblink_queue_reset();
        usblink_reset();
        break;
    }
    refreshPowerState();
}

bool isUsbCableConnected()
{
    return hw_override_get_usb_cable_connected() > 0;
}

void setUsbCableConnected(bool connected)
{
    setUsbPowerSource(connected ? UsbPowerSource::Computer : UsbPowerSource::Disconnected);
}

bool isBatteryPresent()
{
    const int8_t override = hw_override_get_battery_present();
    if (override >= 0)
        return override != 0;
    return true; /* Default: battery present */
}

void setBatteryPresent(bool present)
{
    hw_override_set_battery_present(present ? 1 : 0);
    refreshPowerState();
}

bool isDockAttached()
{
    const int8_t override = hw_override_get_dock_attached();
    return (override >= 0) ? (override != 0) : false;
}

void setDockAttached(bool attached)
{
    hw_override_set_dock_attached(attached ? 1 : 0);
    if (!attached)
        hw_override_set_vsled_mv(0);
    refreshPowerState();
}

int usbBusMillivolts()
{
    const int override = hw_override_get_vbus_mv();
    if (override >= 0)
        return std::clamp(override, 0, 5500);
    return 0;
}

void setUsbBusMillivolts(int millivolts)
{
    hw_override_set_vbus_mv(std::clamp(millivolts, 0, 5500));
    refreshPowerState();
}

int dockRailMillivolts()
{
    const int override = hw_override_get_vsled_mv();
    if (override >= 0)
        return std::clamp(override, 0, 5500);
    /* Dock presence does not imply dock rail power. */
    return 0;
}

void setDockRailMillivolts(int millivolts)
{
    if (!isDockAttached()) {
        hw_override_set_vsled_mv(0);
    } else {
        hw_override_set_vsled_mv(std::clamp(millivolts, 0, 5500));
    }
    refreshPowerState();
}

void pressBackResetButton()
{
    if (!hasPower(isBatteryPresent(), usbPowerSource()))
        return;
    emu_request_reset_hard();
}

} // namespace PowerControl
