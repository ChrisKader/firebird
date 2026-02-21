#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>

#include "app/emuthread.h"
#include "core/emu.h"

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (config_dialog)
        config_dialog->setProperty("visible", false);

    if (flash_dialog)
        flash_dialog->setProperty("visible", false);

    const bool suspendOnClose = qmlBridge()
            ? qmlBridge()->getSuspendOnClose()
            : (settings && settings->value(QStringLiteral("suspendOnClose")).toBool());

    if (!close_after_suspend &&
        suspendOnClose && emuThread().isRunning() && exiting == false)
    {
        close_after_suspend = true;
        qDebug("Suspending...");
        suspend();
        e->ignore();
        return;
    }

    if (emuThread().isRunning() && !emuThread().stop())
        qDebug("Terminating emulator thread failed.");

    // Persist layout/geometry while the full dock tree is still alive.
    savePersistentUiState();

    QMainWindow::closeEvent(e);
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen())
    {
        showNormal();
#ifdef Q_OS_MAC
        // Re-apply rounded corners after leaving fullscreen
        resizeEvent(nullptr);
#endif
    }
    else
    {
#ifdef Q_OS_MAC
        // Clear rounded corner mask in fullscreen
        clearMask();
#endif
        showFullScreen();
    }

    if (auto *action = findChild<QAction *>(QStringLiteral("actionFullscreen")))
        action->setChecked(isFullScreen());
}

void MainWindow::toggleAlwaysOnTop(bool checked)
{
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    show();
    if (settings)
        settings->setValue(QStringLiteral("alwaysOnTop"), checked);
}

void MainWindow::toggleFocusPause(bool checked)
{
    focus_pause_enabled = checked;
    if (settings)
        settings->setValue(QStringLiteral("focusPause"), checked);
}
