#include <cassert>
#include <unistd.h>

#include <QUrl>

#include "emuthread.h"
#include "qmlbridge.h"

#ifndef MOBILE_UI
    #include "mainwindow.h"
#endif

#include "core/emu.h"
#include "core/os/os.h"
#include "core/keypad.h"
#include "core/usb/usblink_queue.h"
#include "ui/screen/framebuffer.h"
#include "ui/input/keypadbridge.h"

namespace {
QMLBridge *g_qml_bridge = nullptr;
}

QMLBridge *qmlBridgeInstance()
{
    return g_qml_bridge;
}

QMLBridge::QMLBridge(EmuThread *emuThread, QObject *parent) : QObject(parent)
#ifdef IS_IOS_BUILD
/* This is needed for iOS, as the app location changes at reinstall */
, settings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/firebird.ini"), QSettings::IniFormat)
#endif
, m_emuThread(emuThread)
{
    assert(g_qml_bridge == nullptr);
    assert(m_emuThread != nullptr);
    g_qml_bridge = this;

    //Migrate old settings
    if(settings.contains(QStringLiteral("usbdir")) && !settings.contains(QStringLiteral("usbdirNew")))
        setUSBDir(QStringLiteral("/") + settings.value(QStringLiteral("usbdir")).toString());

    bool add_default_kit = false;

    // Kits need to be loaded manually
    if(!settings.contains(QStringLiteral("kits")))
    {
        // Migrate
        add_default_kit = true;
    }
    else
    {
        kit_model = settings.value(QStringLiteral("kits")).value<KitModel>();

        // No kits is a bad situation to be in, as kits can only be duplicated...
        if(kit_model.rowCount() == 0)
            add_default_kit = true;
    }

    if(add_default_kit)
    {
        kit_model.addKit(tr("Default"),
                     settings.value(QStringLiteral("boot1")).toString(),
                     settings.value(QStringLiteral("flash")).toString(),
                     settings.value(QStringLiteral("snapshotPath")).toString());
    }

    // Same for debug_on_*
    debug_on_start = getDebugOnStart();
    debug_on_warn = getDebugOnWarn();

    print_on_warn = getPrintOnWarn();

    lcd_scale_mode = static_cast<LCDScaleMode>(getLcdScaleMode());

    connect(&kit_model, &KitModel::anythingChanged, this, &QMLBridge::saveKits, Qt::QueuedConnection);

    setActive(true);
}

QMLBridge::~QMLBridge()
{
    if (g_qml_bridge == this)
        g_qml_bridge = nullptr;
}

EmuThread &QMLBridge::emuThread() const
{
    assert(m_emuThread != nullptr);
    return *m_emuThread;
}

void QMLBridge::setActive(bool b)
{
    if(is_active == b)
        return;

    if(b)
    {
        m_activeEmuConnections.clear();
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::speedChanged, this,
                                              qOverload<double>(&QMLBridge::speedChanged), Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::turboModeChanged, this,
                                              [this](bool) { emit turboModeChanged(); }, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::stopped, this, &QMLBridge::isRunningChanged,
                                              Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::started, this,
                                              [this](bool) { emit isRunningChanged(); }, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::suspended, this,
                                              [this](bool) { emit isRunningChanged(); }, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::resumed, this,
                                              [this](bool) { emit isRunningChanged(); }, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::started, this,
                                              qOverload<bool>(&QMLBridge::started), Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::resumed, this,
                                              qOverload<bool>(&QMLBridge::resumed), Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::suspended, this,
                                              qOverload<bool>(&QMLBridge::suspended), Qt::QueuedConnection));

        // We might have missed some events.
        turboModeChanged();
        speedChanged();
        isRunningChanged();
    }
    else
    {
        for (const QMetaObject::Connection &connection : m_activeEmuConnections)
            QObject::disconnect(connection);
        m_activeEmuConnections.clear();
    }

    is_active = b;
}

void QMLBridge::saveKits()
{
    settings.setValue(QStringLiteral("kits"), QVariant::fromValue(kit_model));
}

void QMLBridge::usblink_progress_changed(int percent, void *qml_bridge_p)
{
    auto *qml_bridge = static_cast<QMLBridge *>(qml_bridge_p);
    if (!qml_bridge)
        return;
    emit qml_bridge->usblinkProgressChanged(percent);
}

void QMLBridge::setTurboMode(bool b)
{
    emuThread().setTurboMode(b);
}

void QMLBridge::speedChanged(double speed)
{
    this->speed = speed;
    emit speedChanged();
}

void QMLBridge::started(bool success)
{
    if(success)
        toastMessage(tr("Emulation started"));
    else
        toastMessage(tr("Couldn't start emulation"));
}

void QMLBridge::resumed(bool success)
{
    if(success)
        toastMessage(tr("Emulation resumed"));
    else
        toastMessage(tr("Could not resume"));
}

void QMLBridge::suspended(bool success)
{
    if(success)
        toastMessage(tr("Flash and snapshot saved")); // When clicking on save, flash is saved as well
    else
        toastMessage(tr("Couldn't save snapshot"));

    emit emuSuspended(success);
}

double QMLBridge::getSpeed()
{
    return speed;
}

bool QMLBridge::getTurboMode()
{
    return turbo_mode;
}

void QMLBridge::notifyButtonStateChanged(int row, int col, bool state)
{
    assert(row < KEYPAD_ROWS);
    assert(col < KEYPAD_COLS);

    emit buttonStateChanged(col + row * KEYPAD_COLS, state);
}

void QMLBridge::touchpadStateChanged()
{
    touchpadStateChanged(float(keypad.touchpad_x)/TOUCHPAD_X_MAX, 1.0f-(float(keypad.touchpad_y)/TOUCHPAD_Y_MAX), keypad.touchpad_contact, keypad.touchpad_down);
}
