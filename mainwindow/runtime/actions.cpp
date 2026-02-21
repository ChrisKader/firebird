#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QStandardPaths>

#include "core/power/powercontrol.h"
#include "core/debug.h"
#include "core/debug_api.h"
#include "core/emu.h"
#include "core/flash.h"
#include "core/gif.h"
#include "core/misc.h"
#include "debugger/dockmanager.h"
#include "debugger/hwconfig/hwconfigwidget.h"
#include "ui/framebuffer.h"
#include "ui_mainwindow.h"

static bool likelyCx2StartupKit(QMLBridge *bridge)
{
    if (!bridge)
        return false;

    KitModel *model = bridge->getKitModel();
    if (!model || model->rowCount() <= 0)
        return false;

    int kitId = bridge->getCurrentKitId();
    if (kitId < 0)
        kitId = static_cast<int>(bridge->getDefaultKit());

    int row = bridge->kitIndexForID(static_cast<unsigned int>(kitId));
    if (row < 0)
        row = 0;

    const QString type = model->getDataRow(row, KitModel::TypeRole).toString();
    return type.contains(QStringLiteral("CX II"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("CX2"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("CX 2"), Qt::CaseInsensitive);
}

void MainWindow::showSpeed(double value)
{
    if (status_bar_speed_label)
        status_bar_speed_label->setText(tr("Speed: %1 %").arg(value * 100, 1, 'f', 0));
}

void MainWindow::screenshot()
{
    QImage image = renderFramebuffer();
    QApplication::clipboard()->setImage(image);
    showStatusMsg(tr("Screenshot copied to clipboard"));
}

void MainWindow::screenshotToFile()
{
    QImage image = renderFramebuffer();

    // Ask for scale factor
    QStringList scales = {QStringLiteral("1x (320x240)"), QStringLiteral("2x (640x480)"),
                          QStringLiteral("3x (960x720)"), QStringLiteral("4x (1280x960)")};
    bool ok = false;
    QString choice = QInputDialog::getItem(this, tr("Screenshot Scale"), tr("Select scale factor:"),
                                           scales, 0, false, &ok);
    if (!ok)
        return;

    int scale = scales.indexOf(choice) + 1;
    if (scale > 1)
        image = image.scaled(image.width() * scale, image.height() * scale,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QString filename = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), QString(), tr("PNG images (*.png)"));
    if (filename.isEmpty())
        return;

    if (!image.save(filename, "PNG"))
        QMessageBox::critical(this, tr("Screenshot failed"), tr("Failed to save screenshot!"));
}

void MainWindow::recordGIF()
{
    static QString path;

    if (path.isEmpty())
    {
        path = QDir::tempPath() + QDir::separator() + QStringLiteral("firebird_tmp.gif");

        gif_start_recording(path.toStdString().c_str(), 3);
    }
    else
    {
        if (gif_stop_recording())
        {
            QString filename = QFileDialog::getSaveFileName(this, tr("Save Recording"), QString(), tr("GIF images (*.gif)"));
            if (filename.isEmpty())
                QFile(path).remove();
            else
            {
                QFile(filename).remove();
                QFile(path).rename(filename);
            }
        }
        else
            QMessageBox::warning(this, tr("Failed recording GIF"), tr("A failure occured during recording"));

        path = QString();
    }

    ui->actionRecord_GIF->setChecked(!path.isEmpty());
}

void MainWindow::launchIdaInstantDebugging()
{
    if (!qmlBridge() || !qmlBridge()->getGDBEnabled())
    {
        QMessageBox::warning(this, tr("GDB server disabled"),
                             tr("Enable the GDB server in settings before launching IDA."));
        return;
    }

    QString ida_path = settings ? settings->value(QStringLiteral("ida_binary_path")).toString() : QString();
    if (ida_path.isEmpty() || !QFileInfo::exists(ida_path))
    {
        ida_path = QFileDialog::getOpenFileName(this, tr("Select IDA executable"));
        if (ida_path.isEmpty())
            return;
        if (settings)
            settings->setValue(QStringLiteral("ida_binary_path"), ida_path);
    }

    QString last_input = settings ? settings->value(QStringLiteral("ida_last_input")).toString() : QString();
    QString input_path = QFileDialog::getOpenFileName(this, tr("Select IDA input file"), last_input);
    if (input_path.isEmpty())
    {
        const auto choice = QMessageBox::question(this, tr("No input file"),
                                                  tr("Launch IDA without an input file?"));
        if (choice != QMessageBox::Yes)
            return;
    }
    else if (settings)
    {
        settings->setValue(QStringLiteral("ida_last_input"), input_path);
    }

    const QString host = settings ? settings->value(QStringLiteral("ida_gdb_host"),
                                                   QStringLiteral("127.0.0.1")).toString()
                                  : QStringLiteral("127.0.0.1");
    const int port = qmlBridge()->getGDBPort();

    const QString r_arg = QStringLiteral("-rgdb@%1:%2").arg(host).arg(port);
    QStringList args;
    args << r_arg;
    if (!input_path.isEmpty())
        args << input_path;

    auto *proc = new QProcess(this);
    proc->start(ida_path, args);
    if (!proc->waitForStarted())
    {
        QMessageBox::warning(this, tr("Launch failed"),
                             tr("Failed to launch IDA at %1 (%2)")
                                 .arg(ida_path, proc->errorString()));
        proc->deleteLater();
    }
}

void MainWindow::connectUSB()
{
    const bool cableConnected = PowerControl::isUsbCableConnected();
    PowerControl::setUsbCableConnected(!cableConnected);
    usblinkChanged(PowerControl::isUsbCableConnected());
}

void MainWindow::usblinkChanged(bool state)
{
    const bool was_connected = m_usbUiConnected;
    m_usbUiConnected = state;

    ui->actionConnect->setText(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->actionConnect->setChecked(state);
    ui->buttonUSB->setToolTip(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->buttonUSB->setChecked(state);

    // Auto-refresh file browser once when USB data link transitions to connected.
    if(state && !was_connected && ui->usblinkTree)
        ui->usblinkTree->wantToReload();
}

void MainWindow::setExtLCD(bool state)
{
    if (!m_dock_ext_lcd)
        return;

    if (state) {
        m_dock_ext_lcd->setFloating(true);
        m_dock_ext_lcd->show();
        m_dock_ext_lcd->raise();
    } else {
        m_dock_ext_lcd->hide();
    }

    if (ui && ui->actionLCD_Window)
        ui->actionLCD_Window->setChecked(m_dock_ext_lcd->isVisible());
}

bool MainWindow::resume()
{
    /* If there's no kit set, use the default kit */
    if (qmlBridge()->getCurrentKitId() == -1)
        qmlBridge()->useDefaultKit();

    if (likelyCx2StartupKit(qmlBridge())) {
        /* CX II should start with no external accessories unless the user
         * actively toggles them after boot. Clear stale persisted rails/state
         * right before launching emulation. */
        hw_override_set_usb_otg_cable(0);
        hw_override_set_usb_cable_connected(0);
        hw_override_set_vbus_mv(0);
        hw_override_set_dock_attached(0);
        hw_override_set_vsled_mv(0);
        PowerControl::refreshPowerState();
        usblinkChanged(false);
    }

    applyQMLBridgeSettings();

    auto snapshot_path = qmlBridge()->getSnapshotPath();
    if (!snapshot_path.isEmpty())
        return resumeFromPath(snapshot_path);
    else
    {
        QMessageBox::warning(this, tr("Can't resume"), tr("The current kit does not have a snapshot file configured"));
        return false;
    }
}

void MainWindow::suspend()
{
    auto snapshot_path = qmlBridge()->getSnapshotPath();
    if (!snapshot_path.isEmpty())
        suspendToPath(snapshot_path);
    else
        QMessageBox::warning(this, tr("Can't suspend"), tr("The current kit does not have a snapshot file configured"));
}

void MainWindow::resumeFromFile()
{
    QString snapshot = QFileDialog::getOpenFileName(this, tr("Select snapshot to resume from"));
    if (!snapshot.isEmpty())
        resumeFromPath(snapshot);
}

void MainWindow::suspendToFile()
{
    QString snapshot = QFileDialog::getSaveFileName(this, tr("Select snapshot to suspend to"));
    if (!snapshot.isEmpty())
        suspendToPath(snapshot);
}

static QString stateSlotPath(int slot)
{
    /* Slots 1..9 live next to the active kit snapshot when available.
     * If no kit snapshot is configured, fall back to app data storage so
     * quick-save/load still works for ad-hoc sessions. */
    QMLBridge *bridge = qmlBridgeInstance();
    QString snapshot_path = bridge ? bridge->getSnapshotPath() : QString();
    QString dir;
    if (!snapshot_path.isEmpty())
        dir = QFileInfo(snapshot_path).absolutePath();
    else
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    return dir + QDir::separator() + QStringLiteral("slot_%1.fbsnapshot").arg(slot);
}

void MainWindow::saveStateSlot(int slot)
{
    QString path = stateSlotPath(slot);
    suspendToPath(path);
    showStatusMsg(tr("Saving state to slot %1...").arg(slot));
}

void MainWindow::loadStateSlot(int slot)
{
    QString path = stateSlotPath(slot);
    if (!QFileInfo::exists(path))
    {
        showStatusMsg(tr("Slot %1 is empty").arg(slot));
        return;
    }
    resumeFromPath(path);
}

void MainWindow::saveFlash()
{
    flash_save_changes();
}

void MainWindow::createFlash()
{
    if (!flash_dialog)
        flash_dialog = flash_dialog_component->create();

    if (!flash_dialog)
        qWarning() << "Could not create flash dialog!";
    else
        flash_dialog->setProperty("visible", QVariant(true));
}

void MainWindow::setUIEditMode(bool e)
{
    settings->setValue(QStringLiteral("uiEditModeEnabled"), e);

    if (m_debugDocks) m_debugDocks->setEditMode(e);
}

void MainWindow::showAbout()
{
    aboutDialog.show();
}

void MainWindow::isBusy(bool busy)
{
    if (busy)
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    else
        QApplication::restoreOverrideCursor();
}

void MainWindow::started(bool success)
{
    debug_invalidate_cpu_snapshot();
    updateUIActionState(success);

    if (success) {
        showStatusMsg(tr("Emulation started"));
        if (m_hwConfig) m_hwConfig->refresh();
    } else {
        QMessageBox::warning(this, tr("Could not start the emulation"), tr("Starting the emulation failed.\nAre the paths to boot1 and flash correct?"));
    }
}

void MainWindow::resumed(bool success)
{
    debug_invalidate_cpu_snapshot();
    updateUIActionState(success);

    if (success) {
        showStatusMsg(tr("Emulation resumed from snapshot"));
        if (m_hwConfig) m_hwConfig->refresh();
    } else {
        QMessageBox::warning(this, tr("Could not resume"), tr("Resuming failed.\nTry to fix the issue and try again."));
    }
}

void MainWindow::suspended(bool success)
{
    if (success)
        showStatusMsg(tr("Snapshot saved"));
    else
        QMessageBox::warning(this, tr("Could not suspend"), tr("Suspending failed.\nTry to fix the issue and try again."));

    if (close_after_suspend)
    {
        if (!success)
            close_after_suspend = false; // May try again
        else
            this->close();
    }
}

void MainWindow::stopped()
{
    debug_invalidate_cpu_snapshot();
    updateUIActionState(false);
    showStatusMsg(tr("Emulation stopped"));
}

void MainWindow::showStatusMsg(QString str)
{
    status_label.setText(str);
}

void MainWindow::kitDataChanged(QModelIndex, QModelIndex, QVector<int> roles)
{
    if (roles.contains(KitModel::NameRole))
    {
        refillKitMenus();

        // Need to update window title if kit is active
        updateWindowTitle();
    }
}

void MainWindow::kitAnythingChanged()
{
    if (qmlBridge()->getKitModel()->rowCount() != ui->menuRestart_with_Kit->actions().size())
        refillKitMenus();
}

void MainWindow::currentKitChanged(const Kit &kit)
{
    (void)kit;
    updateWindowTitle();
}

void MainWindow::refillKitMenus()
{
    ui->menuRestart_with_Kit->clear();
    ui->menuBoot_Diags_with_Kit->clear();

    auto &&kit_model = qmlBridge()->getKitModel();
    for (auto &&kit : kit_model->getKits())
    {
        QAction *action = ui->menuRestart_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, &QAction::triggered, this, &MainWindow::startKit);

        action = ui->menuBoot_Diags_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, &QAction::triggered, this, &MainWindow::startKitDiags);
    }
}

void MainWindow::updateWindowTitle()
{
    // Need to update window title if kit is active
    int kitIndex = qmlBridge()->kitIndexForID(qmlBridge()->getCurrentKitId());
    if (kitIndex >= 0)
    {
        auto name = qmlBridge()->getKitModel()->getKits()[kitIndex].name;
        setWindowTitle(tr("Firebird Emu - %1").arg(name));
    }
    else
        setWindowTitle(tr("Firebird Emu"));
}

void MainWindow::applyQMLBridgeSettings()
{
    // Reload the current kit
    qmlBridge()->useKit(qmlBridge()->getCurrentKitId());

    emuThread().port_gdb = qmlBridge()->getGDBEnabled() ? qmlBridge()->getGDBPort() : 0;
    emuThread().port_rdbg = qmlBridge()->getRDBEnabled() ? qmlBridge()->getRDBPort() : 0;
}

void MainWindow::restart()
{
    /* If there's no kit set, use the default kit */
    if (qmlBridge()->getCurrentKitId() == -1)
        qmlBridge()->useDefaultKit();

    applyQMLBridgeSettings();

    if (emuThread().boot1.isEmpty())
    {
        QMessageBox::critical(this, tr("No boot1 set"), tr("Before you can start the emulation, you have to select a proper boot1 file."));
        return;
    }

    if (emuThread().flash.isEmpty())
    {
        QMessageBox::critical(this, tr("No flash image loaded"), tr("Before you can start the emulation, you have to load a proper flash file.\n"
                                                                    "You can create one via Flash->Create Flash in the menu."));
        return;
    }

    if (emuThread().stop())
        emuThread().start();
    else
        QMessageBox::warning(this, tr("Restart needed"), tr("Failed to restart emulator. Close and reopen this app.\n"));
}

void MainWindow::openConfiguration()
{
    if (!config_dialog)
        config_dialog = config_component->create();

    if (!config_dialog)
        qWarning() << "Could not create config dialog!";
    else
        config_dialog->setProperty("visible", QVariant(true));
}

void MainWindow::startKit()
{
    auto action = qobject_cast<QAction *>(sender());
    if (!action)
    {
        qWarning() << "Received signal from invalid sender";
        return;
    }

    auto kit_id = static_cast<unsigned int>(action->data().toInt());
    qmlBridge()->setCurrentKit(kit_id);
    boot_order = ORDER_BOOT2;
    restart();
}

void MainWindow::startKitDiags()
{
    auto action = qobject_cast<QAction *>(sender());
    if (!action)
    {
        qWarning() << "Received signal from invalid sender";
        return;
    }

    auto kit_id = static_cast<unsigned int>(action->data().toInt());
    qmlBridge()->setCurrentKit(kit_id);
    boot_order = ORDER_DIAGS;
    restart();
}

void MainWindow::xmodemSend()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Select file to send"));
    if (filename.isEmpty())
        return;

    std::string path = filename.toStdString();
    xmodem_send(path.c_str());
}

void MainWindow::switchToMobileUI()
{
    switchUIMode(true);
}

bool QQuickWidgetLessBroken::event(QEvent *event)
{
    if (event->type() == QEvent::Leave)
    {
        QMouseEvent ev(QEvent::MouseMove, QPointF(0, 0), QPointF(0, 0), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QQuickWidget::event(&ev);
    }

    return QQuickWidget::event(event);
}
