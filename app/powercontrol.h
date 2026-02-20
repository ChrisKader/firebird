#ifndef FIREBIRD_POWERCONTROL_H
#define FIREBIRD_POWERCONTROL_H

#include "core/emu.h"

namespace PowerControl {

enum class UsbPowerSource {
    Disconnected = 0,
    Computer,
    Charger,
    OtgCable,
};

UsbPowerSource usbPowerSource();
void setUsbPowerSource(UsbPowerSource source);

bool isUsbCableConnected();
void setUsbCableConnected(bool connected);

bool isBatteryPresent();
void setBatteryPresent(bool present);
void refreshPowerState();
bool isDockAttached();
void setDockAttached(bool attached);
int usbBusMillivolts();
void setUsbBusMillivolts(int millivolts);
int dockRailMillivolts();
void setDockRailMillivolts(int millivolts);

void pressBackResetButton();

} // namespace PowerControl

#endif // FIREBIRD_POWERCONTROL_H
