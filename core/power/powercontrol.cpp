#include "powercontrol.h"

#include <algorithm>

#include "core/usb/usblink.h"
#include "core/usb/usblink_queue.h"

namespace PowerControl {

static constexpr int kMinExternalRailMvForPower = 4500;

int usbBusMillivolts();
bool isDockAttached();
int dockRailMillivolts();

void refreshPowerState()
{
    /* Battery/power state is reported through ADC and PMU registers.
     * The guest firmware decides how to react (low-battery warning,
     * graceful shutdown, sleep, etc.) — the emulator does not force
     * shutdown based on battery voltage. */
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
    emu_request_reset_hard();
}

} // namespace PowerControl
