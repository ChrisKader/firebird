#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QStandardPaths>

#include "core/power/powercontrol.h"
#include "core/debug/debug.h"
#include "core/debug/debug_api.h"
#include "core/emu.h"
#include "core/memory/flash.h"
#include "core/gif.h"
#include "core/misc.h"
#include "core/usblink_queue.h"
#include "ui/widgets/console/consolewidget.h"
#include "ui/docking/manager/dockmanager.h"
#include "ui/widgets/hwconfig/hwconfigwidget.h"
#include "ui/screen/framebuffer.h"
#include "ui/theme/widgettheme.h"
#include "ui_mainwindow.h"

void MainWindow::switchTranslator(const QLocale &locale)
{
    qApp->removeTranslator(&appTranslator);
    // For English, nothing to load after removing the translator.
    if (locale.name() == QStringLiteral("en_US") || (appTranslator.load(locale, QStringLiteral(":/i18n/i18n/")) && qApp->installTranslator(&appTranslator)))
    {
        settings->setValue(QStringLiteral("preferred_lang"), locale.name());
    }
    else
        QMessageBox::warning(this, tr("Language change"), tr("No translation available for this language :("));
}

void MainWindow::changeEvent(QEvent *event)
{
    const auto eventType = event->type();
    if (eventType == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        updateWindowTitle();
        retranslateDocks();
    }
    else if (eventType == QEvent::LocaleChange)
        switchTranslator(QLocale::system());
    else if (eventType == QEvent::ActivationChange && focus_pause_enabled)
    {
        if (!isActiveWindow() && emuThread().isRunning() && !ui->actionPause->isChecked())
        {
            focus_auto_paused = true;
            emuThread().setPaused(true);
        }
        else if (isActiveWindow() && focus_auto_paused)
        {
            focus_auto_paused = false;
            emuThread().setPaused(false);
        }
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QMimeData *mime_data = e->mimeData();
    if (!mime_data->hasUrls())
        return;

    for (auto &&url : mime_data->urls())
    {
        auto local = QDir::toNativeSeparators(url.toLocalFile());
        auto remote = qmlBridge()->getUSBDir() + QLatin1Char('/') + QFileInfo(local).fileName();
        usblink_queue_put_file(local.toStdString(), remote.toStdString(), usblink_progress_callback, this);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() == false)
        return e->ignore();

    for (QUrl &url : e->mimeData()->urls())
    {
        static const QStringList valid_suffixes = {QStringLiteral("tns"),
                                                   QStringLiteral("tno"), QStringLiteral("tnc"),
                                                   QStringLiteral("tco"), QStringLiteral("tcc"),
                                                   QStringLiteral("tco2"), QStringLiteral("tcc2"),
                                                   QStringLiteral("tct2")};

        QFileInfo file(url.fileName());
        if (!valid_suffixes.contains(file.suffix().toLower()))
            return e->ignore();
    }

    e->accept();
}

void MainWindow::serialChar(const char c)
{
    auto emitUart = [this](const QString &out) {
        if (m_dockManager && m_dockManager->console())
            m_dockManager->console()->appendTaggedOutput(ConsoleTag::Uart, out);
    };

    /* Coalesce CRLF into a single newline-stamped record.
     * Keep bare CR behavior for in-place progress updates. */
    if (m_serialPendingCR) {
        if (c == '\n') {
            emitUart(m_serialLineBuf + QStringLiteral("\n"));
            m_serialLineBuf.clear();
            m_serialPendingCR = false;
            return;
        }

        emitUart(m_serialLineBuf + QStringLiteral("\r"));
        m_serialLineBuf.clear();
        m_serialPendingCR = false;
    }

    if (c == '\r') {
        m_serialPendingCR = true;
        return;
    }

    if (c == '\n') {
        emitUart(m_serialLineBuf + QStringLiteral("\n"));
        m_serialLineBuf.clear();
        return;
    }

    m_serialLineBuf += QLatin1Char(c);
}

void MainWindow::debugInputRequested(bool b)
{
    setDebuggerActive(b);
    switchUIMode(false);

    if (b)
    {
        debug_capture_cpu_snapshot();
        if (m_dockManager) m_dockManager->raise();
        if (m_dockManager) {
            m_dockManager->markDirty();
            m_dockManager->refreshAll();
        }
        if (m_dockManager && m_dockManager->console())
            m_dockManager->console()->focusInput();
    } else {
        debug_invalidate_cpu_snapshot();
    }
}

void MainWindow::debuggerEntered(bool entered)
{
    if (!gdb_connected)
        return;

    setDebuggerActive(entered);
    if (entered)
    {
        debug_capture_cpu_snapshot();
        if (m_dockManager) m_dockManager->raise();
        if (m_dockManager) {
            m_dockManager->markDirty();
            m_dockManager->refreshAll();
        }
        if (m_dockManager && m_dockManager->console())
            m_dockManager->console()->focusInput();
    }
    else
    {
        debug_invalidate_cpu_snapshot();
        if (m_dockManager) m_dockManager->hideAutoShown();
    }
}

void MainWindow::debugStr(QString str)
{
    if (m_dockManager && m_dockManager->console()) {
        if (str.startsWith(QLatin1Char('>'))) {
            /* Command echo from debug line edit -- plain text, no tag */
            m_dockManager->console()->appendOutput(str);
        } else {
            /* Debug engine output -- tagged and syntax-highlighted */
            m_dockManager->console()->appendTaggedOutput(ConsoleTag::Debug, str);
        }

    }
}

void MainWindow::nlogStr(QString str)
{
    if (m_dockManager && m_dockManager->console())
        m_dockManager->console()->appendTaggedOutput(ConsoleTag::Nlog, str);
}


void MainWindow::setDebuggerActive(bool active)
{
    debugger_active = active;
    if (debugger_toggle_button)
    {
        debugger_toggle_button->setCheckable(true);
        debugger_toggle_button->setChecked(active);
        debugger_toggle_button->setToolTip(active ? tr("Continue (send 'c')") : tr("Enter debugger"));
    }
    if (status_bar_debug_label)
    {
        status_bar_debug_label->setVisible(active);
        if (active)
        {
            const WidgetTheme &t = currentWidgetTheme();
            status_bar_debug_label->setText(QStringLiteral("  DEBUGGER  "));
            status_bar_debug_label->setStyleSheet(
                QStringLiteral("QLabel { background-color: %1; color: %2; "
                               "border-radius: 3px; padding: 1px 6px; font-weight: bold; font-size: 10px; }")
                    .arg(t.markerBreakpoint.name(), t.selectionText.name()));
        }
    }
}

void MainWindow::usblinkDownload(int progress)
{
    usblinkProgress(progress);

    if (progress < 0)
        QMessageBox::warning(this, tr("Download failed"), tr("Could not download file."));
}

void MainWindow::usblinkProgress(int progress)
{
    if (progress < 0 || progress > 100)
        progress = 0; // No error handling here

    emit usblink_progress_changed(progress);
}

void MainWindow::usblink_progress_callback(int progress, void *userData)
{
    MainWindow *mw = static_cast<MainWindow *>(userData);
    if (!mw || !mw->ui)
        return;

    // TODO: Don't do a full refresh
    // Also refresh on error, in case of multiple transfers
    if ((progress == 100 || progress < 0) && usblink_queue_size() == 0)
        mw->ui->usblinkTree->wantToReload(); // Reload the file explorer after uploads finished

    if (progress < 0 || progress > 100)
        progress = 0; // No error handling here

    emit mw->usblink_progress_changed(progress);
}

void MainWindow::switchUIMode(bool mobile_ui)
{
    if (!mobileui_dialog && mobile_ui)
        mobileui_dialog = mobileui_component->create();

    if (mobileui_dialog)
        mobileui_dialog->setProperty("visible", mobile_ui);
    else if (mobile_ui)
    {
        qWarning() << "Could not create mobile UI!";
        return; // Do not switch the UI mode as the mobile UI could not be created
    }

    qmlBridge()->setActive(mobile_ui);
    this->setActive(!mobile_ui);

    settings->setValue(QStringLiteral("lastUIMode"), mobile_ui ? 1 : 0);
}

void MainWindow::setActive(bool b)
{
    if (b == is_active)
        return;

    is_active = b;

    if (b)
    {
        m_activeEmuConnections.clear();
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::speedChanged, this, &MainWindow::showSpeed, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::turboModeChanged, ui->buttonSpeed, &QPushButton::setChecked, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::usblinkChanged, this, &MainWindow::usblinkChanged, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::started, this, &MainWindow::started, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::paused, ui->actionPause, &QAction::setChecked, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::resumed, this, &MainWindow::resumed, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::suspended, this, &MainWindow::suspended, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::stopped, this, &MainWindow::stopped, Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::lcdFrameReady, ui->lcdView, qOverload<>(&LCDWidget::update), Qt::QueuedConnection));
        m_activeEmuConnections.append(connect(&emuThread(), &EmuThread::lcdFrameReady, &lcd, qOverload<>(&LCDWidget::update), Qt::QueuedConnection));

        // We might have missed a few events
        updateUIActionState(emuThread().isRunning());
        ui->buttonSpeed->setChecked(turbo_mode);
        usblinkChanged(usblink_connected);
    }
    else
    {
        for (const QMetaObject::Connection &connection : m_activeEmuConnections)
            QObject::disconnect(connection);
        m_activeEmuConnections.clear();

        // Close the config dialog
        if (config_dialog)
            config_dialog->setProperty("visible", false);
    }

    setVisible(b);
}

void MainWindow::suspendToPath(QString path)
{
    emuThread().suspend(path);
}

bool MainWindow::resumeFromPath(QString path)
{
    if (!emuThread().resume(path))
    {
        QMessageBox::warning(this, tr("Could not resume"), tr("Try to restart this app."));
        return false;
    }

    return true;
}

void MainWindow::changeProgress(int value)
{
    ui->progressBar->setValue(value);
}

void MainWindow::updateUIActionState(bool emulation_running)
{
    ui->actionReset->setEnabled(emulation_running);
    ui->actionPause->setEnabled(emulation_running);
    ui->actionRestart->setText(emulation_running ? tr("Re&start") : tr("&Start"));
    ui->actionRestart->setToolTip(emulation_running ? tr("Restart") : tr("Start"));
    ui->buttonPlayPause->setToolTip(emulation_running ? tr("Restart") : tr("Start"));

    ui->actionScreenshot->setEnabled(emulation_running);
    ui->actionRecord_GIF->setEnabled(emulation_running);
    ui->actionConnect->setEnabled(emulation_running);
    ui->actionDebugger->setEnabled(emulation_running);
    ui->actionXModem->setEnabled(emulation_running);
    ui->actionLeavePTT->setEnabled(emulation_running);

    ui->actionSuspend->setEnabled(emulation_running);
    ui->actionSuspend_to_file->setEnabled(emulation_running);
    ui->actionSave->setEnabled(emulation_running);

    ui->buttonSpeed->setEnabled(true);
}

void MainWindow::retranslateDocks()
{
    // The tab-based docks are not handled by mainwindow.ui but got created by
    // convertTabsToDocks() above, so translation needs to be done manually.
    const auto dockChildren = content_window->findChildren<DockWidget *>();
    for (DockWidget *dw : dockChildren)
    {
        if (dw->widget() == ui->tab)
            dw->setWindowTitle(tr("Keypad"));
        else if (dw->widget() == ui->tabFiles)
            dw->setWindowTitle(tr("File Transfer"));
    }
    if (m_dock_lcd) {
        int percent = qRound(qMin(ui->lcdView->width() / 320.0, ui->lcdView->height() / 240.0) * 100.0);
        m_dock_lcd->setWindowTitle(tr("Screen") + QStringLiteral(" (%1%)").arg(percent));
    }
    if (m_dock_controls)
        m_dock_controls->setWindowTitle(tr("Controls"));
    if (m_dockManager) m_dockManager->retranslate();
}
