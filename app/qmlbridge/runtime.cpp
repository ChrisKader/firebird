#include "app/qmlbridge.h"

#include <QFile>
#include <QFileInfo>
#include <QScopedPointer>
#include <QUrl>

#include "app/emuthread.h"
#include "core/flash.h"
#include "core/keypad.h"
#include "core/os/os.h"
#include "core/usblink_queue.h"
#include "ui/keypadbridge.h"

#ifndef MOBILE_UI
    #include "mainwindow.h"
#endif

void QMLBridge::setButtonState(int id, bool state)
{
    // Called from QML button clicks.  NButton.qml uses a _fromCpp guard so
    // the C++ -> QML -> C++ round-trip (via notifyButtonStateChanged) is
    // suppressed, avoiding duplicate keyStateChanged emissions.
    setKeypad(static_cast<unsigned int>(id), state);
}

void QMLBridge::setTouchpadState(qreal x, qreal y, bool contact, bool down)
{
    ::touchpad_set_state(x, y, contact, down);

    touchpadStateChanged();
}

bool QMLBridge::isMobile()
{
    #ifdef MOBILE_UI
        return true;
    #else
        return false;
    #endif
}

void QMLBridge::sendFile(QUrl url, QString dir)
{
    auto local = toLocalFile(url);
    auto remote = dir + QLatin1Char('/') + basename(local);
    usblink_queue_put_file(local.toStdString(), remote.toStdString(), QMLBridge::usblink_progress_changed, this);
}

void QMLBridge::sendExitPTT()
{
    usblink_queue_new_dir("/Press-to-Test", nullptr, nullptr);
    usblink_queue_put_file(std::string(), "/Press-to-Test/Exit Test Mode.tns", QMLBridge::usblink_progress_changed, this);
}

QString QMLBridge::basename(QString path)
{
    if(path.isEmpty())
        return tr("None");

#ifdef Q_OS_ANDROID
    QScopedPointer<char, QScopedPointerPodDeleter> android_bn{android_basename(path.toUtf8().data())};
    if(android_bn)
        return QString::fromUtf8(android_bn.data());
#endif

    return QFileInfo(path).fileName();
}

QUrl QMLBridge::dir(QString path)
{
    return QUrl{QUrl::fromLocalFile(path).toString(QUrl::RemoveFilename)};
}

QString QMLBridge::toLocalFile(QUrl url)
{
    // Pass through Android content url, see fopen_utf8
    if(url.scheme() == QStringLiteral("content"))
        return url.toString(QUrl::FullyEncoded);

    return url.toLocalFile();
}

bool QMLBridge::fileExists(QString path)
{
    return QFile::exists(path);
}

int QMLBridge::kitIndexForID(unsigned int id)
{
    return kit_model.indexForID(id);
}

#ifndef MOBILE_UI
void QMLBridge::switchUIMode(bool mobile_ui)
{
    MainWindow *window = m_mainWindow.data();
    if (window)
        window->switchUIMode(mobile_ui);
}

void QMLBridge::setMainWindow(MainWindow *window)
{
    m_mainWindow = window;
}
#endif

bool QMLBridge::createFlash(QString path, int productID, int featureValues, QString manuf, QString boot2, QString os, QString diags)
{
    bool is_cx = productID >= 0x0F0;
    std::string preload_str[4] = { manuf.toStdString(), boot2.toStdString(), diags.toStdString(), os.toStdString() };
    const char *preload[4] = { nullptr, nullptr, nullptr, nullptr };

    for(unsigned int i = 0; i < 4; ++i)
        if(preload_str[i] != "")
            preload[i] = preload_str[i].c_str();

    uint8_t *nand_data = nullptr;
    size_t nand_size;

    if(!flash_create_new(is_cx, preload, productID, featureValues, is_cx, &nand_data, &nand_size))
    {
        free(nand_data);
        return false;
    }

    QFile flash_file(path);
    if(!flash_file.open(QFile::WriteOnly) || !flash_file.write(reinterpret_cast<char*>(nand_data), nand_size))
    {
        free(nand_data);
        return false;
    }

    free(nand_data);

    flash_file.close();
    return true;
}

QString QMLBridge::componentDescription(QString path, QString expected_type)
{
    FILE *file = fopen_utf8(path.toUtf8().data(), "rb");
    if(!file)
        return tr("Open failed");

    std::string type, version;
    bool b = flash_component_info(file, type, version);
    fclose(file);
    if(!b)
        return QStringLiteral("???");

    if(type != expected_type.toStdString())
        return tr("Found %1 instead").arg(QString::fromStdString(type).trimmed());

    return QString::fromStdString(version);
}

QString QMLBridge::manufDescription(QString path)
{
    FILE *file = fopen_utf8(path.toUtf8().data(), "rb");
    if(!file)
        return tr("Open failed");

    auto raw_type = flash_read_type(file, true);
    fclose(file);
    // Reading or parsing failed
    if(raw_type == "" || raw_type == "???")
        return QStringLiteral("???");

    return QString::fromStdString(raw_type);
}

QString QMLBridge::osDescription(QString path)
{
    FILE *file = fopen_utf8(path.toUtf8().data(), "rb");
    if(!file)
        return tr("Open failed");

    std::string version;
    bool b = flash_os_info(file, version);
    fclose(file);
    if(!b)
        return QStringLiteral("???");

    return QString::fromStdString(version);
}

bool QMLBridge::restart()
{
    if(emuThread().isRunning() && !emuThread().stop())
    {
        toastMessage(tr("Could not stop emulation"));
        return false;
    }

    emuThread().port_gdb = getGDBEnabled() ? getGDBPort() : 0;
    emuThread().port_rdbg = getRDBEnabled() ? getRDBPort() : 0;

    if(!emuThread().boot1.isEmpty() && !emuThread().flash.isEmpty()) {
        toastMessage(tr("Starting emulation"));
        emuThread().start();
        return true;
    } else {
        toastMessage(tr("No boot1 or flash selected.\nSwipe keypad left for configuration."));
        return false;
    }
}

void QMLBridge::setPaused(bool b)
{
    emuThread().setPaused(b);
}

void QMLBridge::reset()
{
    emuThread().reset();
}

void QMLBridge::suspend()
{
    toastMessage(tr("Suspending emulation"));
    auto snapshot_path = getSnapshotPath();
    if(!snapshot_path.isEmpty())
        emuThread().suspend(snapshot_path);
    else {
        toastMessage(tr("The current kit does not have a snapshot file configured"));
        emit emuSuspended(false);
    }
}

void QMLBridge::resume()
{
    toastMessage(tr("Resuming emulation"));

    emuThread().port_gdb = getGDBEnabled() ? getGDBPort() : 0;
    emuThread().port_rdbg = getRDBEnabled() ? getRDBPort() : 0;

    auto snapshot_path = getSnapshotPath();
    if(!snapshot_path.isEmpty())
        emuThread().resume(snapshot_path);
    else
        toastMessage(tr("The current kit does not have a snapshot file configured"));
}

bool QMLBridge::useDefaultKit()
{
    if(setCurrentKit(getDefaultKit()))
        return true;

    setCurrentKit(kit_model.getKits()[0].id); // Use first kit as fallback
    return false;
}

bool QMLBridge::setCurrentKit(unsigned int id)
{
    const Kit *kit = useKit(id);
    if(!kit)
         return false;

    current_kit_id = id;
    emit currentKitChanged(*kit);

    return true;
}

int QMLBridge::getCurrentKitId()
{
    return current_kit_id;
}

const Kit *QMLBridge::useKit(unsigned int id)
{
    int kitIndex = kitIndexForID(id);
    if(kitIndex < 0)
        return nullptr;

    auto &&kit = kit_model.getKits()[kitIndex];
    emuThread().boot1 = kit.boot1;
    emuThread().flash = kit.flash;
    fallback_snapshot_path = kit.snapshot;

    return &kit;
}

bool QMLBridge::stop()
{
    return emuThread().stop();
}

bool QMLBridge::saveFlash()
{
    return flash_save_changes();
}

QString QMLBridge::getBoot1Path()
{
    return emuThread().boot1;
}

QString QMLBridge::getFlashPath()
{
    return emuThread().flash;
}

QString QMLBridge::getSnapshotPath()
{
    int kitIndex = kitIndexForID(current_kit_id);
    if(kitIndex >= 0)
        return kit_model.getKits()[kitIndex].snapshot;
    else
        return fallback_snapshot_path;
}
