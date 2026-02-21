#include "mainwindow.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QKeySequence>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>

#include <cstring>

#include "app/qmlbridge.h"
#include "core/memory/flash.h"
#include "core/memory/mem.h"
#include "core/misc.h"
#include "transfer/usblinktreewidget.h"
#include "ui/theme/materialicons.h"
#include "ui_mainwindow.h"

void MainWindow::setupActionAndMenuWiring()
{
    // Emu -> GUI (QueuedConnection as they're different threads)
    connect(&emuThread(), &EmuThread::serialChar, this, &MainWindow::serialChar, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::debugStr, this, &MainWindow::debugStr, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::nlogStr, this, &MainWindow::nlogStr, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::isBusy, this, &MainWindow::isBusy, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::statusMsg, this, &MainWindow::showStatusMsg, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::debugInputRequested, this, &MainWindow::debugInputRequested, Qt::QueuedConnection);
    connect(&emuThread(), &EmuThread::debuggerEntered, this, &MainWindow::debuggerEntered, Qt::QueuedConnection);

    // GUI -> Emu (no QueuedConnection possible, watch out!)
    connect(this, &MainWindow::debuggerCommand, &emuThread(), &EmuThread::debuggerInput);

    // Menu "Emulator"
    connect(ui->buttonReset, &QToolButton::clicked, &emuThread(), &EmuThread::reset);
    connect(ui->actionReset, &QAction::triggered, &emuThread(), &EmuThread::reset);
    connect(ui->actionRestart, &QAction::triggered, this, &MainWindow::restart);
    connect(ui->actionDebugger, &QAction::triggered, &emuThread(), &EmuThread::enterDebugger);
    connect(ui->actionLaunch_IDA, &QAction::triggered, this, &MainWindow::launchIdaInstantDebugging);
    if (ui->actionLaunch_IDA) {
        ui->actionLaunch_IDA->setToolTip(tr("Experimental: launch IDA and attach to Firebird GDB server"));
        ui->actionLaunch_IDA->setStatusTip(tr("Experimental feature; not covered by automated tests."));
    }
    connect(ui->actionConfiguration, &QAction::triggered, this, &MainWindow::openConfiguration);
    connect(ui->actionPause, &QAction::toggled, &emuThread(), &EmuThread::setPaused);
    connect(ui->buttonSpeed, &QPushButton::clicked, &emuThread(), &EmuThread::setTurboMode);

    // F11 = fullscreen toggle
    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, &MainWindow::toggleFullscreen);

    // Fullscreen menu item
    {
        auto *fullscreenAction = new QAction(tr("&Fullscreen"), this);
        fullscreenAction->setObjectName(QStringLiteral("actionFullscreen"));
        fullscreenAction->setCheckable(true);
        connect(fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);
        ui->menuTools->addAction(fullscreenAction);
    }

    // Always-on-top toggle (settings loaded after QSettings init below)
    {
        auto *alwaysOnTopAction = new QAction(tr("Always on &Top"), this);
        alwaysOnTopAction->setObjectName(QStringLiteral("actionAlwaysOnTop"));
        alwaysOnTopAction->setCheckable(true);
        connect(alwaysOnTopAction, &QAction::toggled, this, &MainWindow::toggleAlwaysOnTop);
        ui->menuTools->addAction(alwaysOnTopAction);
    }

    // Focus-pause toggle (settings loaded after QSettings init below)
    {
        auto *focusPauseAction = new QAction(tr("Pause on &Focus Loss"), this);
        focusPauseAction->setObjectName(QStringLiteral("actionFocusPause"));
        focusPauseAction->setCheckable(true);
        connect(focusPauseAction, &QAction::toggled, this, &MainWindow::toggleFocusPause);
        ui->menuTools->addAction(focusPauseAction);
    }

    // Menu "Tools"
    connect(ui->buttonScreenshot, &QToolButton::clicked, this, &MainWindow::screenshot);
    connect(ui->actionScreenshot, &QAction::triggered, this, &MainWindow::screenshot);
    ui->actionScreenshot->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    {
        auto *saveScreenshotAction = new QAction(tr("Save Screenshot..."), this);
        saveScreenshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
        connect(saveScreenshotAction, &QAction::triggered, this, &MainWindow::screenshotToFile);
        ui->menuTools->insertAction(ui->actionRecord_GIF, saveScreenshotAction);
    }
    connect(ui->actionRecord_GIF, &QAction::triggered, this, &MainWindow::recordGIF);
    connect(ui->actionConnect, &QAction::triggered, this, &MainWindow::connectUSB);
    connect(ui->buttonUSB, &QToolButton::clicked, this, &MainWindow::connectUSB);
    connect(ui->actionLCD_Window, &QAction::triggered, this, &MainWindow::setExtLCD);
    connect(ui->actionXModem, &QAction::triggered, this, &MainWindow::xmodemSend);
    connect(ui->actionSwitch_to_Mobile_UI, &QAction::triggered, this, &MainWindow::switchToMobileUI);
    connect(ui->actionLeavePTT, &QAction::triggered, qmlBridge(), &QMLBridge::sendExitPTT);
    ui->actionConnect->setShortcut(QKeySequence(Qt::Key_F10));
    ui->actionConnect->setAutoRepeat(false);

    // Menu "State"
    connect(ui->actionResume, &QAction::triggered, this, &MainWindow::resume);
    connect(ui->actionSuspend, &QAction::triggered, this, &MainWindow::suspend);
    connect(ui->actionResume_from_file, &QAction::triggered, this, &MainWindow::resumeFromFile);
    connect(ui->actionSuspend_to_file, &QAction::triggered, this, &MainWindow::suspendToFile);

    // Snapshot slots 1-9
    {
        ui->menuState->addSeparator();
        auto *saveSlotMenu = ui->menuState->addMenu(tr("Save to Slot"));
        auto *loadSlotMenu = ui->menuState->addMenu(tr("Load from Slot"));
        for (int i = 1; i <= 9; i++) {
            auto *saveAction = saveSlotMenu->addAction(tr("Slot &%1").arg(i));
            saveAction->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + i)));
            connect(saveAction, &QAction::triggered, this, [this, i]() { saveStateSlot(i); });

            auto *loadAction = loadSlotMenu->addAction(tr("Slot &%1").arg(i));
            loadAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | static_cast<Qt::Key>(Qt::Key_0 + i)));
            connect(loadAction, &QAction::triggered, this, [this, i]() { loadStateSlot(i); });
        }
    }

    // Menu "Flash"
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveFlash);
    connect(ui->actionCreate_flash, &QAction::triggered, this, &MainWindow::createFlash);

    // ROM/RAM export/import
    {
        ui->menuFlash->addSeparator();
        auto *exportROM = ui->menuFlash->addAction(tr("Export Flash Image..."));
        connect(exportROM, &QAction::triggered, this, [this]() {
            QString path = QFileDialog::getSaveFileName(this, tr("Export Flash Image"), QString(),
                                                         tr("Binary files (*.bin);;All files (*)"));
            if (path.isEmpty()) return;
            if (flash_save_as(path.toStdString().c_str()) != 0)
                QMessageBox::warning(this, tr("Export Failed"), tr("Could not write flash image."));
            else
                showStatusMsg(tr("Flash image exported"));
        });

        auto *exportRAM = ui->menuFlash->addAction(tr("Export RAM Image..."));
        connect(exportRAM, &QAction::triggered, this, [this]() {
            QString path = QFileDialog::getSaveFileName(this, tr("Export RAM Image"), QString(),
                                                         tr("Binary files (*.bin);;All files (*)"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, tr("Export Failed"), tr("Could not write file."));
                return;
            }
            // SDRAM region from mem_areas[1]
            uint32_t ram_size = mem_areas[1].size;
            uint8_t *ram_ptr = (uint8_t *)phys_mem_ptr(mem_areas[1].base, ram_size);
            if (ram_ptr)
                f.write(reinterpret_cast<char *>(ram_ptr), ram_size);
            showStatusMsg(tr("RAM image exported (%1 MB)").arg(ram_size / (1024 * 1024)));
        });

        ui->menuFlash->addSeparator();
        auto *nandBrowserAction = ui->menuFlash->addAction(tr("NAND Browser..."));
        connect(nandBrowserAction, &QAction::triggered, this, [this]() {
            if (m_dock_nand) {
                m_dock_nand->setVisible(true);
                m_dock_nand->raise();
            }
        });

        auto *importRAM = ui->menuFlash->addAction(tr("Import RAM Image..."));
        connect(importRAM, &QAction::triggered, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this, tr("Import RAM Image"), QString(),
                                                         tr("Binary files (*.bin);;All files (*)"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Import Failed"), tr("Could not read file."));
                return;
            }
            QByteArray data = f.readAll();
            uint32_t ram_size = mem_areas[1].size;
            uint32_t copy_size = qMin((uint32_t)data.size(), ram_size);
            uint8_t *ram_ptr = (uint8_t *)phys_mem_ptr(mem_areas[1].base, copy_size);
            if (ram_ptr) {
                memcpy(ram_ptr, data.constData(), copy_size);
                showStatusMsg(tr("RAM image imported (%1 bytes)").arg(copy_size));
            }
        });
    }

    // Menu "About"
    connect(ui->actionAbout_Firebird, &QAction::triggered, this, &MainWindow::showAbout);
    connect(ui->actionAbout_Qt, &QAction::triggered, qApp, &QApplication::aboutQt);

    /* -- Set Material icons on menu actions -------------------- */
    {
        using namespace MaterialIcons;
        const QColor fg = palette().color(QPalette::WindowText);
        auto mi = [&](ushort cp) { return fromCodepoint(material_icon_font, cp, fg); };

        ui->actionRestart->setIcon(mi(CP::Play));
        ui->actionReset->setIcon(mi(CP::Refresh));
        ui->actionDebugger->setIcon(mi(CP::BugReport));
        ui->actionConfiguration->setIcon(mi(CP::Settings));
        ui->actionPause->setIcon(mi(CP::Pause));
        ui->actionScreenshot->setIcon(mi(CP::Screenshot));
        ui->actionConnect->setIcon(mi(CP::USB));
        ui->actionRecord_GIF->setIcon(mi(CP::Image));
        ui->actionLCD_Window->setIcon(mi(CP::Display));
        ui->actionResume->setIcon(mi(CP::Play));
        ui->actionSuspend->setIcon(mi(CP::Save));
        ui->actionSave->setIcon(mi(CP::Save));
        ui->actionCreate_flash->setIcon(mi(CP::Add));
        if (ui->refreshButton) {
            ui->refreshButton->setIcon(mi(CP::Refresh));
            ui->refreshButton->setText(QString());
            ui->refreshButton->setToolTip(tr("Refresh file list"));
        }
    }

    // Lang switch
    QStringList translations = QDir(QStringLiteral(":/i18n/i18n/")).entryList();
    translations << QStringLiteral("en_US.qm"); // Equal to no translation
    for (auto &languageCode : translations)
    {
        languageCode.chop(3); // Chop off file extension
        QLocale locale(languageCode);
        QAction *action = new QAction(locale.nativeLanguageName(), ui->menuLanguage);
        connect(action, &QAction::triggered, this, [this, languageCode]
                { this->switchTranslator(QLocale(languageCode)); });
        ui->menuLanguage->addAction(action);
    }

    // File transfer
    connect(ui->refreshButton, &QToolButton::clicked, ui->usblinkTree, &USBLinkTreeWidget::reloadFilebrowser);
    connect(ui->usblinkTree, &USBLinkTreeWidget::downloadProgress, this, &MainWindow::usblinkDownload, Qt::QueuedConnection);
    connect(ui->usblinkTree, &USBLinkTreeWidget::uploadProgress, this, &MainWindow::changeProgress, Qt::QueuedConnection);
    connect(this, &MainWindow::usblink_progress_changed, this, &MainWindow::changeProgress, Qt::QueuedConnection);

    // QMLBridge
    KitModel *model = qmlBridge()->getKitModel();
    connect(model, &KitModel::anythingChanged, this, &MainWindow::kitAnythingChanged);
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
                kitDataChanged(topLeft, bottomRight, QVector<int>(roles.begin(), roles.end()));
            });
    connect(qmlBridge(), &QMLBridge::currentKitChanged, this, &MainWindow::currentKitChanged);
}

void MainWindow::initializePersistentSettingsAndState()
{
    // Set up monospace fonts
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setStyleHint(QFont::Monospace);

    // Without this line, Qt prints warning messages...
    qRegisterMetaType<QVector<int>>();

#ifdef Q_OS_ANDROID
    // On android the settings file is deleted everytime you update or uninstall,
    // so choose a better, safer, location
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    settings = new QSettings(path + QStringLiteral("/nspire_emu_thread.ini"), QSettings::IniFormat);
#else
    settings = new QSettings();
#endif

    QString prefLang = settings->value(QStringLiteral("preferred_lang"), QStringLiteral("none")).toString();
    if (prefLang != QStringLiteral("none"))
        switchTranslator(QLocale(prefLang));
    else if (appTranslator.load(QLocale::system(), QStringLiteral(":/i18n/i18n/")))
        qApp->installTranslator(&appTranslator);

    updateUIActionState(false);

    // Load settings for window management actions
    if (auto *aot = findChild<QAction *>(QStringLiteral("actionAlwaysOnTop")))
        aot->setChecked(settings->value(QStringLiteral("alwaysOnTop"), false).toBool());
    if (auto *fp = findChild<QAction *>(QStringLiteral("actionFocusPause"))) {
        focus_pause_enabled = settings->value(QStringLiteral("focusPause"), false).toBool();
        fp->setChecked(focus_pause_enabled);
    }

    restoreStartupLayoutFromSettings();
    restoreHardwareOverridesFromSettings();

    refillKitMenus();

    ui->lcdView->setFocus();

    // Ensure dock buttons/theme are refreshed after docks are created.
    applyWidgetTheme();
}

void MainWindow::finalizeStartupSequence()
{
    // Select default Kit
    bool defaultKitFound = qmlBridge()->useDefaultKit();

    if (qmlBridge()->getKitModel()->allKitsEmpty())
    {
        // Do not show the window before MainWindow gets shown,
        // otherwise it won't be in focus.
        QTimer::singleShot(0, this, &MainWindow::openConfiguration);

        switchUIMode(true);

        return;
    }

    if (settings->value(QStringLiteral("lastUIMode"), 1).toUInt() == 1)
        switchUIMode(true);
    else
    {
        switchUIMode(false);
        show();
    }

    if (!qmlBridge()->getAutostart())
    {
        showStatusMsg(tr("Start the emulation via Emulation->Start."));
        return;
    }

    // Autostart handling
    if (!defaultKitFound)
        showStatusMsg(tr("Default Kit not found"));
    else
    {
        bool resumed = false;
        if (!qmlBridge()->getSnapshotPath().isEmpty())
            resumed = resume();

        if (!resumed)
        {
            // Boot up normally
            if (!emuThread().boot1.isEmpty() && !emuThread().flash.isEmpty())
                restart();
            else
                showStatusMsg(tr("Start the emulation via Emulation->Start."));
        }
    }
}
