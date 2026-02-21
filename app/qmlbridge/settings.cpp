#include "app/qmlbridge.h"

#include <QVersionNumber>

#include "app/emuthread.h"
#include "core/emu.h"
#include "ui/screen/framebuffer.h"

unsigned int QMLBridge::getGDBPort()
{
    return settings.value(QStringLiteral("gdbPort"), 3333).toInt();
}

void QMLBridge::setGDBPort(unsigned int port)
{
    if(getGDBPort() == port)
        return;

    settings.setValue(QStringLiteral("gdbPort"), port);
    emit gdbPortChanged();
}

bool QMLBridge::getGDBEnabled()
{
    return settings.value(QStringLiteral("gdbEnabled"), !isMobile()).toBool();
}

void QMLBridge::setGDBEnabled(bool e)
{
    if(getGDBEnabled() == e)
        return;

    settings.setValue(QStringLiteral("gdbEnabled"), e);
    emit gdbEnabledChanged();
}

unsigned int QMLBridge::getRDBPort()
{
    return settings.value(QStringLiteral("rdbgPort"), 3334).toInt();
}

void QMLBridge::setRDBPort(unsigned int port)
{
    if(getRDBPort() == port)
        return;

    settings.setValue(QStringLiteral("rdbgPort"), port);
    emit rdbPortChanged();
}

bool QMLBridge::getRDBEnabled()
{
    return settings.value(QStringLiteral("rdbgEnabled"), !isMobile()).toBool();
}

void QMLBridge::setRDBEnabled(bool e)
{
    if(getRDBEnabled() == e)
        return;

    settings.setValue(QStringLiteral("rdbgEnabled"), e);
    emit rdbEnabledChanged();
}

bool QMLBridge::getDebugOnWarn()
{
    return settings.value(QStringLiteral("debugOnWarn"), !isMobile()).toBool();
}

void QMLBridge::setDebugOnWarn(bool e)
{
    if(getDebugOnWarn() == e)
        return;

    debug_on_warn = e;
    settings.setValue(QStringLiteral("debugOnWarn"), e);
    emit debugOnWarnChanged();
}

bool QMLBridge::getDebugOnStart()
{
    return settings.value(QStringLiteral("debugOnStart"), false).toBool();
}

void QMLBridge::setDebugOnStart(bool e)
{
    if(getDebugOnStart() == e)
        return;

    debug_on_start = e;
    settings.setValue(QStringLiteral("debugOnStart"), e);
    emit debugOnStartChanged();
}

void QMLBridge::setPrintOnWarn(bool p)
{
    if (getPrintOnWarn() == p)
        return;

    print_on_warn = p;
    settings.setValue(QStringLiteral("printOnWarn"), p);
    emit printOnWarnChanged();
}

bool QMLBridge::getPrintOnWarn()
{
    return settings.value(QStringLiteral("printOnWarn"), true).toBool();
}

bool QMLBridge::getAutostart()
{
    return settings.value(QStringLiteral("emuAutostart"), true).toBool();
}

void QMLBridge::setAutostart(bool e)
{
    if(getAutostart() == e)
        return;

    settings.setValue(QStringLiteral("emuAutostart"), e);
    emit autostartChanged();
}

bool QMLBridge::getDarkTheme()
{
    return settings.value(QStringLiteral("darkTheme"), true).toBool();
}

void QMLBridge::setDarkTheme(bool enabled)
{
    if (getDarkTheme() == enabled)
        return;

    settings.setValue(QStringLiteral("darkTheme"), enabled);
    emit darkThemeChanged();
}

unsigned int QMLBridge::getDefaultKit()
{
    return settings.value(QStringLiteral("defaultKit"), 0).toUInt();
}

void QMLBridge::setDefaultKit(unsigned int id)
{
    if(getDefaultKit() == id)
        return;

    settings.setValue(QStringLiteral("defaultKit"), id);
    emit defaultKitChanged();
}

bool QMLBridge::getLeftHanded()
{
    return settings.value(QStringLiteral("leftHanded"), false).toBool();
}

void QMLBridge::setLeftHanded(bool e)
{
    if(getLeftHanded() == e)
        return;

    settings.setValue(QStringLiteral("leftHanded"), e);
    emit leftHandedChanged();
}

bool QMLBridge::getSuspendOnClose()
{
    return settings.value(QStringLiteral("suspendOnClose"), true).toBool();
}

void QMLBridge::setSuspendOnClose(bool e)
{
    if(getSuspendOnClose() == e)
        return;

    settings.setValue(QStringLiteral("suspendOnClose"), e);
    emit suspendOnCloseChanged();
}

QString QMLBridge::getUSBDir()
{
    return settings.value(QStringLiteral("usbdirNew"), QStringLiteral("/ndless")).toString();
}

void QMLBridge::setUSBDir(QString dir)
{
    if(getUSBDir() == dir)
        return;

    settings.setValue(QStringLiteral("usbdirNew"), dir);
    emit usbDirChanged();
}

bool QMLBridge::getIsRunning()
{
    return emuThread().isRunning();
}

QString QMLBridge::getVersion()
{
    #define STRINGIFYMAGIC(x) #x
    #define STRINGIFY(x) STRINGIFYMAGIC(x)
    return QStringLiteral(STRINGIFY(FB_VERSION));
}

int QMLBridge::getLcdScaleMode()
{
    return settings.value(QStringLiteral("lcdScaleMode"), static_cast<int>(LCDScaleMode::Bilinear)).toInt();
}

void QMLBridge::setLcdScaleMode(int mode)
{
    if (getLcdScaleMode() == mode)
        return;

    settings.setValue(QStringLiteral("lcdScaleMode"), mode);
    lcd_scale_mode = static_cast<LCDScaleMode>(mode);
    emit lcdScaleModeChanged();
}

int QMLBridge::getMobileX()
{
    return settings.value(QStringLiteral("mobileX"), -1).toInt();
}

void QMLBridge::setMobileX(int x)
{
    settings.setValue(QStringLiteral("mobileX"), x);
}

int QMLBridge::getMobileY()
{
    return settings.value(QStringLiteral("mobileY"), -1).toInt();
}

void QMLBridge::setMobileY(int y)
{
    settings.setValue(QStringLiteral("mobileY"), y);
}

int QMLBridge::getMobileWidth()
{
    return settings.value(QStringLiteral("mobileWidth"), -1).toInt();
}

void QMLBridge::setMobileWidth(int w)
{
    settings.setValue(QStringLiteral("mobileWidth"), w);
}

int QMLBridge::getMobileHeight()
{
    return settings.value(QStringLiteral("mobileHeight"), -1).toInt();
}

void QMLBridge::setMobileHeight(int h)
{
    settings.setValue(QStringLiteral("mobileHeight"), h);
}

bool QMLBridge::saveDialogSupported()
{
    #ifdef Q_OS_ANDROID
        // Starting with Qt 5.13, the Android "File Picker" is used, but that doesn't allow creation of files.
        return QVersionNumber::fromString(QString::fromUtf8(qVersion())) < QVersionNumber(5, 13);
    #else
        return true;
    #endif
}
