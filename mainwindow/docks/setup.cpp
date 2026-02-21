#include "mainwindow.h"

#include <QActionGroup>
#include <QDesktopServices>
#include <QFile>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QUrl>

#include "ui/docking/manager/dockmanager.h"
#include "ui/widgets/hwconfig/hwconfigwidget.h"
#include "ui/widgets/nandbrowser/nandbrowserwidget.h"
#include "mainwindow/layout_persistence.h"
#include "ui_mainwindow.h"

static constexpr const char *kSettingLayoutProfile = "layoutProfile";
static constexpr const char *kSettingDockFocusPolicy = "dockFocusPolicy";

static constexpr const char *kDockExternalLCD = "dockExternalLCD";
static constexpr const char *kDockNandBrowser = "dockNandBrowser";
static constexpr const char *kDockHwConfig = "dockHwConfig";

void MainWindow::convertTabsToDocks()
{
    /* Legacy name kept for compatibility with existing call sites.
     * This function is the authoritative dock construction routine for
     * desktop UI mode and runs before layout restore. */
    /* STEP 1: Build dock-management menu and layout actions. */
    // Create "Docks" menu to make closing and opening docks more intuitive
    QMenu *docks_menu = new QMenu(tr("Docks"), this);
    ui->menubar->insertMenu(ui->menuAbout->menuAction(), docks_menu);

    QMenu *edit_menu = new QMenu(tr("&Edit"), this);
    ui->menubar->insertMenu(ui->menuTools->menuAction(), edit_menu);

    m_undoLayoutAction = edit_menu->addAction(tr("Undo Layout"));
    m_undoLayoutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Z")));
    connect(m_undoLayoutAction, &QAction::triggered, this, &MainWindow::undoLayoutChange);

    m_redoLayoutAction = edit_menu->addAction(tr("Redo Layout"));
    m_redoLayoutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+Z")));
    connect(m_redoLayoutAction, &QAction::triggered, this, &MainWindow::redoLayoutChange);
    updateLayoutHistoryActions();

    QAction *editmode_toggle = new QAction(tr("Enable UI edit mode"), this);
    editmode_toggle->setCheckable(true);
    editmode_toggle->setChecked(settings->value(QStringLiteral("uiEditModeEnabled"), true).toBool());
    connect(editmode_toggle, &QAction::toggled, this, &MainWindow::setUIEditMode);

    docks_menu->addAction(editmode_toggle);

    QAction *resetLayoutAction = new QAction(tr("Reset Layout"), this);
    docks_menu->addAction(resetLayoutAction);

    QMenu *layouts_menu = docks_menu->addMenu(tr("Layouts"));

    const auto saveLayoutProfileAction = [this](const QString &profileName) {
        const QJsonObject debugDockState = m_dockManager ? m_dockManager->serializeDockStates()
                                                        : QJsonObject();
        const QJsonObject coreDockConnections = serializeCoreDockConnections();
        QString error;
        if (!saveLayoutProfile(content_window,
                               profileName,
                               debugDockState,
                               coreDockConnections,
                               &error)) {
            QMessageBox::warning(this, tr("Save layout failed"),
                                 tr("Could not save layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        showStatusMsg(tr("Saved layout profile '%1'").arg(profileName));
    };

    const auto loadLayoutProfileAction = [this](const QString &profileName) {
        QString error;
        QJsonObject debugDockState;
        QJsonObject coreDockConnections;
        if (!restoreLayoutProfile(content_window,
                                  profileName,
                                  &error,
                                  &debugDockState,
                                  &coreDockConnections)) {
            if (profileName == QLatin1String("default")) {
                resetDockLayout();
                settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
                if (m_dockManager)
                    m_dockManager->refreshIcons();
                showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
                return;
            }
            QMessageBox::warning(this, tr("Load layout failed"),
                                 tr("Could not load layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }

        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        if (m_dockManager && !debugDockState.isEmpty())
            m_dockManager->restoreDockStates(debugDockState);
        restoreCoreDockConnections(coreDockConnections);
        if (m_dockManager)
            m_dockManager->refreshIcons();
        showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
    };

    const auto resetToLastSavedLayoutAction = [this, loadLayoutProfileAction]() {
        QString profileName = settings
                ? settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed()
                : QString();
        if (profileName.isEmpty())
            profileName = QStringLiteral("default");
        loadLayoutProfileAction(profileName);
    };
    connect(resetLayoutAction, &QAction::triggered, this,
            [resetToLastSavedLayoutAction]() { resetToLastSavedLayoutAction(); });

    QAction *loadDefaultLayoutAction = layouts_menu->addAction(tr("Load Default"));
    QAction *loadDebugLayoutAction = layouts_menu->addAction(tr("Load Debugging"));
    QAction *loadWidescreenLayoutAction = layouts_menu->addAction(tr("Load Widescreen"));
    QAction *loadCustomLayoutAction = layouts_menu->addAction(tr("Load Custom"));
    layouts_menu->addSeparator();
    QAction *resetToBaselineLayoutAction = layouts_menu->addAction(tr("Reset to Baseline"));
    layouts_menu->addSeparator();
    QAction *saveDefaultLayoutAction = layouts_menu->addAction(tr("Save As Default"));
    QAction *saveDebugLayoutAction = layouts_menu->addAction(tr("Save As Debugging"));
    QAction *saveWidescreenLayoutAction = layouts_menu->addAction(tr("Save As Widescreen"));
    QAction *saveCustomLayoutAction = layouts_menu->addAction(tr("Save As Custom"));
    layouts_menu->addSeparator();
    QAction *openLayoutFolderAction = layouts_menu->addAction(tr("Open Layout Folder"));

    connect(loadDefaultLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("default")); });
    connect(loadDebugLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("debugging")); });
    connect(loadWidescreenLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("widescreen")); });
    connect(loadCustomLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("custom")); });
    connect(resetToBaselineLayoutAction, &QAction::triggered, this, [this]() {
        resetDockLayout();
        if (m_dockManager)
            m_dockManager->refreshIcons();
        showStatusMsg(tr("Reset layout to baseline"));
    });
    connect(saveDefaultLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("default")); });
    connect(saveDebugLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("debugging")); });
    connect(saveWidescreenLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("widescreen")); });
    connect(saveCustomLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("custom")); });
    connect(openLayoutFolderAction, &QAction::triggered, this, [this]() {
        QString error;
        if (!ensureLayoutProfilesDir(&error)) {
            QMessageBox::warning(this, tr("Open layout folder failed"),
                                 tr("Could not open layout folder: %1").arg(error));
            return;
        }
        const QString dirPath = layoutProfilesDirPath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath))) {
            QMessageBox::warning(this, tr("Open layout folder failed"),
                                 tr("Could not open layout folder: %1").arg(dirPath));
        }
    });

    connect(layouts_menu, &QMenu::aboutToShow, this,
            [loadDefaultLayoutAction, loadDebugLayoutAction,
             loadWidescreenLayoutAction, loadCustomLayoutAction]() {
        loadDefaultLayoutAction->setEnabled(true);
        loadDebugLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("debugging"))));
        loadWidescreenLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("widescreen"))));
        loadCustomLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("custom"))));
    });

    QMenu *focusMenu = docks_menu->addMenu(tr("Dock Focus Policy"));
    QActionGroup *focusGroup = new QActionGroup(focusMenu);
    focusGroup->setExclusive(true);

    QAction *focusAlwaysAction = focusMenu->addAction(tr("Always Raise"));
    focusAlwaysAction->setCheckable(true);
    focusAlwaysAction->setData(static_cast<int>(DockManager::DockFocusPolicy::Always));
    focusGroup->addAction(focusAlwaysAction);

    QAction *focusExplicitAction = focusMenu->addAction(tr("Raise on Explicit Actions"));
    focusExplicitAction->setCheckable(true);
    focusExplicitAction->setData(static_cast<int>(DockManager::DockFocusPolicy::ExplicitOnly));
    focusGroup->addAction(focusExplicitAction);

    QAction *focusNeverAction = focusMenu->addAction(tr("Never Raise Automatically"));
    focusNeverAction->setCheckable(true);
    focusNeverAction->setData(static_cast<int>(DockManager::DockFocusPolicy::Never));
    focusGroup->addAction(focusNeverAction);

    auto applyDockFocusPolicy = [this](int value) {
        DockManager::DockFocusPolicy policy = DockManager::DockFocusPolicy::Always;
        if (value == static_cast<int>(DockManager::DockFocusPolicy::ExplicitOnly))
            policy = DockManager::DockFocusPolicy::ExplicitOnly;
        else if (value == static_cast<int>(DockManager::DockFocusPolicy::Never))
            policy = DockManager::DockFocusPolicy::Never;
        settings->setValue(QString::fromLatin1(kSettingDockFocusPolicy), static_cast<int>(policy));
        if (m_dockManager)
            m_dockManager->setDockFocusPolicy(policy);
    };
    connect(focusGroup, &QActionGroup::triggered, this, [applyDockFocusPolicy](QAction *action) {
        if (!action)
            return;
        applyDockFocusPolicy(action->data().toInt());
    });

    docks_menu->addSeparator();

    /* STEP 2: Convert hidden legacy tabs into regular docks. */
    struct TabDockPair
    {
        QWidget *tab = nullptr;
        DockWidget *dock = nullptr;
    };

    QVector<TabDockPair> dock_pairs;
    while (ui->tabWidget->count())
    {
        QWidget *tab = ui->tabWidget->widget(0);
        const QString tab_title = ui->tabWidget->tabText(0);
        const QIcon tab_icon = ui->tabWidget->tabIcon(0);
        ui->tabWidget->removeTab(0);

        DockWidget *dw = createMainDock(tab_title,
                                        tab,
                                        tab->objectName(), /* stable saveState identity */
                                        Qt::RightDockWidgetArea,
                                        docks_menu,
                                        tab_icon,
                                        true,
                                        tab != ui->tab,
                                        tab == ui->tab);
        dock_pairs.append({tab, dw});
    }

    DockWidget *dock_files = nullptr;
    DockWidget *dock_keypad = nullptr;

    /* Place converted legacy tab pages as regular docks (no dedicated sidebar column). */
    for (const auto &pair : dock_pairs)
    {
        QWidget *tab = pair.tab;
        DockWidget *dw = pair.dock;

        if (tab == ui->tabFiles)         dock_files = dw;
        else if (tab == ui->tab)         dock_keypad = dw;
    }

    /* Keep pointers for layout reset/re-link behavior. */
    m_dock_files = dock_files;
    m_dock_keypad = dock_keypad;

    if (!m_dockManager)
        m_dockManager = std::make_unique<DockManager>(content_window, material_icon_font, this);
    m_dockManager->registerMainDock(DockManager::MainDockId::Files, m_dock_files);
    m_dockManager->registerMainDock(DockManager::MainDockId::Keypad, m_dock_keypad);
    m_dockManager->registerMainDock(DockManager::MainDockId::Screen, m_dock_lcd);
    m_dockManager->registerMainDock(DockManager::MainDockId::Controls, m_dock_controls);

    /* STEP 3: Create utility docks that were not tab pages. */
    /* Create NAND Browser dock */
    m_nandBrowser = new NandBrowserWidget(content_window);
    m_dock_nand = createMainDock(tr("NAND Browser"),
                                 m_nandBrowser,
                                 QString::fromLatin1(kDockNandBrowser),
                                 Qt::RightDockWidgetArea,
                                 docks_menu,
                                 QIcon(),
                                 true,
                                 true,
                                 false);
    m_dockManager->registerMainDock(DockManager::MainDockId::NandBrowser, m_dock_nand);

    /* Create Hardware Configuration dock */
    m_hwConfig = new HwConfigWidget(content_window);
    m_dock_hwconfig = createMainDock(tr("Hardware Config"),
                                     m_hwConfig,
                                     QString::fromLatin1(kDockHwConfig),
                                     Qt::RightDockWidgetArea,
                                     docks_menu,
                                     QIcon(),
                                     true,
                                     true,
                                     false);
    m_dockManager->registerMainDock(DockManager::MainDockId::HardwareConfig, m_dock_hwconfig);

    /* External LCD as an optional floating dock (instead of a separate window). */
    m_dock_ext_lcd = createMainDock(tr("Screen (External)"),
                                    &lcd,
                                    QString::fromLatin1(kDockExternalLCD),
                                    Qt::RightDockWidgetArea,
                                    docks_menu,
                                    QIcon(),
                                    false,
                                    true,
                                    false);
    m_dockManager->registerMainDock(DockManager::MainDockId::ExternalScreen, m_dock_ext_lcd);
    m_dock_ext_lcd->setFloating(true);
    m_dock_ext_lcd->hide();
    connect(m_dock_ext_lcd, &DockWidget::visibilityChanged, this, [this](bool visible) {
        if (ui && ui->actionLCD_Window)
            ui->actionLCD_Window->setChecked(visible);
    });

    /* Add LCD and Controls dock toggle actions to the Docks menu */
    if (m_dock_lcd) {
        docks_menu->addAction(m_dock_lcd->toggleViewAction());
    }
    if (m_dock_controls) {
        docks_menu->addAction(m_dock_controls->toggleViewAction());
    }

    /* STEP 4: Wire post-dock-creation links that depend on dock objects. */
    /* -- LCD/Keypad Link ---------------------------------------- */
    if (m_dock_keypad) {
        /* QQuickWidget's Shape.CurveRenderer loses GPU state when the
         * widget is reparented during dock/undock.  Reload the QML
         * source to recreate all Shape items with fresh resources. */
        connect(m_dock_keypad, &DockWidget::topLevelChanged, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                auto src = ui->keypadWidget->source();
                ui->keypadWidget->setSource(QUrl());
                ui->keypadWidget->setSource(src);
            });
        });
    }

    // Keep default corner behavior so all docks behave like regular Qt docks.

    /* STEP 5: Create debugger docks and finalize initial dock visibility. */
    /* Create the CEmu-style debugger docks via DockManager */
    m_dockManager->createDocks(docks_menu);
    connect(m_dockManager.get(), &DockManager::debugCommand,
            this, &MainWindow::debuggerCommand);

    int savedFocusPolicy = settings->value(QString::fromLatin1(kSettingDockFocusPolicy),
                                           static_cast<int>(DockManager::DockFocusPolicy::Always)).toInt();
    if (savedFocusPolicy < static_cast<int>(DockManager::DockFocusPolicy::Always) ||
        savedFocusPolicy > static_cast<int>(DockManager::DockFocusPolicy::Never)) {
        savedFocusPolicy = static_cast<int>(DockManager::DockFocusPolicy::Always);
    }
    applyDockFocusPolicy(savedFocusPolicy);
    for (QAction *action : focusGroup->actions()) {
        if (action && action->data().toInt() == savedFocusPolicy) {
            action->setChecked(true);
            break;
        }
    }

    setUIEditMode(editmode_toggle->isChecked());

    if (!m_layoutHistoryTimer) {
        m_layoutHistoryTimer = new QTimer(this);
        m_layoutHistoryTimer->setSingleShot(true);
        m_layoutHistoryTimer->setInterval(150);
        connect(m_layoutHistoryTimer, &QTimer::timeout, this, &MainWindow::captureLayoutHistorySnapshot);
    }
    const auto dockChildren = content_window->findChildren<DockWidget *>();
    for (DockWidget *dock : dockChildren) {
        connect(dock, &DockWidget::dockLocationChanged, this,
                [this](Qt::DockWidgetArea) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                });
        connect(dock, &DockWidget::topLevelChanged, this,
                [this](bool) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                    applyConnectedCoreDocks(nullptr, false);
                });
        connect(dock, &DockWidget::visibilityChanged, this,
                [this](bool) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                });
    }

    for (DockWidget *dock : coreGroupableDocks()) {
        if (!dock)
            continue;
        dock->installEventFilter(this);
    }
    scheduleCoreDockConnectOverlayRefresh();

    ui->tabWidget->setHidden(true);
}
