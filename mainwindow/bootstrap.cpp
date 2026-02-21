#include "mainwindow.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWidgetAction>
#include <QQmlComponent>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/KDDockWidgets.h>
#endif

#include "app/qmlbridge.h"
#include "core/debug.h"
#include "core/flash.h"
#include "core/mem.h"
#include "core/misc.h"
#include "debugger/dockmanager.h"
#include "debugger/hwconfig/hwconfigwidget.h"
#include "transfer/usblinktreewidget.h"
#include "ui/framebuffer.h"
#include "ui/keypadbridge.h"
#include "ui/materialicons.h"
#include "ui_mainwindow.h"

class AdaptiveControlsWidget : public QWidget
{
public:
    explicit AdaptiveControlsWidget(QWidget *parent = nullptr)
        : QWidget(parent),
          m_outerLayout(new QVBoxLayout(this)),
          m_stripWidget(new QWidget(this)),
          m_layout(new QBoxLayout(QBoxLayout::LeftToRight, m_stripWidget))
    {
        m_outerLayout->setContentsMargins(0, 0, 0, 0);
        m_outerLayout->setSpacing(0);
        m_outerLayout->addStretch(1);
        m_outerLayout->addWidget(m_stripWidget, 0, Qt::AlignCenter);
        m_outerLayout->addStretch(1);

        m_layout->setContentsMargins(2, 0, 2, 0);
        m_layout->setSpacing(3);
        m_layout->setAlignment(Qt::AlignCenter);
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        refreshDirection();
    }

    void addControl(QWidget *widget)
    {
        if (!widget)
            return;
        tuneControl(widget);
        m_layout->addWidget(widget, 0, Qt::AlignCenter);
        refreshDirection();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        refreshDirection();
    }

    QSize minimumSizeHint() const override
    {
        return QSize(0, stripHeightHint());
    }

    QSize sizeHint() const override
    {
        return QSize(0, stripHeightHint());
    }

private:
    int stripHeightHint() const
    {
        if (!m_layout)
            return 1;
        return qMax(1, m_layout->sizeHint().height());
    }

    void tuneControl(QWidget *widget)
    {
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            button->setMinimumHeight(24);
        }
    }

    void refreshDirection()
    {
        const int tightHeight = stripHeightHint();
        m_layout->setDirection(QBoxLayout::LeftToRight);
        m_layout->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
        m_stripWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_stripWidget->setMinimumHeight(tightHeight);
        m_stripWidget->setMaximumHeight(tightHeight);
        m_stripWidget->setMinimumWidth(0);
        setMinimumHeight(tightHeight);
        setMaximumHeight(QWIDGETSIZE_MAX);
        updateGeometry();
    }

    QVBoxLayout *m_outerLayout = nullptr;
    QWidget *m_stripWidget = nullptr;
    QBoxLayout *m_layout = nullptr;
};

EmuThread &MainWindow::emuThread() const
{
    Q_ASSERT(m_emuThread != nullptr);
    return *m_emuThread;
}

MainWindow::MainWindow(QMLBridge *qmlBridgeDep, EmuThread *emuThreadDep, QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_qmlBridge(qmlBridgeDep),
      m_emuThread(emuThreadDep)
{
    Q_ASSERT(m_qmlBridge);
    Q_ASSERT(m_emuThread);

#ifdef Q_OS_MAC
    // Use native title bar so traffic lights (close/minimize/maximize) render correctly.
    // Custom header buttons are hidden on macOS since the native ones appear in the title bar.
#endif

    ui->setupUi(this);
    // Make the central content fill the full area between header and status bar
    if (ui->mainLayout)
    {
        ui->mainLayout->setContentsMargins(0, 0, 0, 0);
        ui->mainLayout->setSpacing(0);
    }

    // Load a Material-style icon font with a fallback to the TTF variant if the OTF fails.
    auto loadIconFont = [](const QString &path) -> QFont
    {
        const int fontId = QFontDatabase::addApplicationFont(path);
        if (fontId < 0)
        {
            qWarning() << "Failed to load icon font from" << path;
            return {};
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty())
        {
            qWarning() << "Icon font has no families after load:" << path;
            return {};
        }

        QFont font(families.first());
        font.setPixelSize(18);
        qDebug() << "Loaded icon font" << path << "family" << families.first();
        return font;
    };

    QFont materialIconFont = loadIconFont(QStringLiteral(":/fonts/MaterialIconsRound-Regular.otf"));
    if (materialIconFont.family().isEmpty())
        materialIconFont = loadIconFont(QStringLiteral(":/fonts/MaterialSymbolsRounded.ttf"));
    material_icon_font = materialIconFont;

    auto applyMaterialBugIcon = [materialIconFont](QToolButton *button)
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setFont(materialIconFont);
        button->setText(QString(QChar(0xE868))); // "bug_report"
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    };

    auto applyMaterialGlyph = [materialIconFont](QToolButton *button, ushort codepoint, const QString &toolTip = QString())
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setIcon(QIcon());
        button->setFont(materialIconFont);
        button->setText(QString(QChar(codepoint)));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        if (!toolTip.isEmpty())
            button->setToolTip(toolTip);
    };

    auto applyMaterialGlyphPush = [materialIconFont](QPushButton *button, ushort codepoint, const QString &toolTip = QString())
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setIcon(QIcon());
        button->setFont(materialIconFont);
        button->setText(QString(QChar(codepoint)));
        if (!toolTip.isEmpty())
            button->setToolTip(toolTip);
    };

    auto applyThemeGlyph = [applyMaterialGlyph](QToolButton *button, bool darkEnabled)
    {
        // Material Symbols codepoints for dark_mode / light_mode
        const ushort glyph = darkEnabled ? 0xE51C : 0xE518;
        applyMaterialGlyph(button, glyph, darkEnabled ? QObject::tr("Switch to light mode") : QObject::tr("Switch to dark mode"));
    };

    // Add a compact "Debugger" toggle button into the custom header bar,
    // similar to VS Code's header controls. (Currently disabled)


    // Apply Material glyphs to main control buttons if the font is available.
    applyMaterialGlyph(ui->buttonPlayPause, 0xE037, tr("Start"));
    applyMaterialGlyph(ui->buttonReset, 0xE5D5, tr("Reset"));
    applyMaterialGlyph(ui->buttonScreenshot, 0xE412, tr("Screenshot"));
    applyMaterialGlyph(ui->buttonUSB, 0xE1E0, tr("Connect USB"));
    QSize controlSize = ui->buttonPlayPause->sizeHint();
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonReset->sizeHint().width()));
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonScreenshot->sizeHint().width()));
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonUSB->sizeHint().width()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonReset->sizeHint().height()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonScreenshot->sizeHint().height()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonUSB->sizeHint().height()));
    const QSize compactControlSize(qMax(28, controlSize.width() - 4),
                                   qMax(24, controlSize.height() - 6));
    for (QToolButton *button : {ui->buttonPlayPause, ui->buttonReset, ui->buttonScreenshot, ui->buttonUSB}) {
        if (!button)
            continue;
        button->setMinimumSize(compactControlSize);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }
    ui->buttonSpeed->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    ui->buttonSpeed->setMinimumSize(compactControlSize);
    applyMaterialGlyphPush(ui->buttonSpeed, 0xE9E4, tr("Toggle turbo mode"));
    ui->buttonSpeed->setCheckable(true);

    // Unified play/pause/start toggle.
    updatePlayPauseButtonFn = [this, applyMaterialGlyph]()
    {
        const bool running = ui->actionPause->isEnabled();
        const bool paused = ui->actionPause->isChecked();
        const bool playing = running && !paused;
        ushort glyph = 0xE037; // play
        QString tip = tr("Start");
        if (running)
        {
            if (paused)
            {
                glyph = 0xE037;
                tip = tr("Resume");
            }
            else
            {
                glyph = 0xE034;
                tip = tr("Pause");
            }
        }
        else
        {
            tip = ui->actionRestart->text().isEmpty() ? tr("Start") : ui->actionRestart->text().remove(QLatin1Char('&'));
        }
        applyMaterialGlyph(ui->buttonPlayPause, glyph, tip);
        ui->buttonPlayPause->setChecked(playing);
        ui->buttonPlayPause->setEnabled(ui->actionRestart->isEnabled() || running);
    };
    updatePlayPauseButtonFn();
    connect(ui->actionPause, &QAction::toggled, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->actionPause, &QAction::changed, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->actionRestart, &QAction::changed, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->buttonPlayPause, &QToolButton::clicked, this, [this]()
            {
        const bool running = ui->actionPause->isEnabled();
        if (!running) {
            ui->actionRestart->trigger();
        } else {
            ui->actionPause->trigger();
        } });
    connect(&emuThread(), &EmuThread::paused, this, [this](bool)
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });

    // Create an inner main window that will host all docks and the LCD frame.
    // This lets the custom header bar sit above everything else (similar to VS Code),
    // while docks live around the central emulator surface without overlapping the header.
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    const auto contentWindowOptions = KDDockWidgets::MainWindowOptions(
        KDDockWidgets::MainWindowOption_HasCentralWidget |
        KDDockWidgets::MainWindowOption_CentralWidgetGetsAllExtraSpace);
    content_window = new KDDockWidgets::QtWidgets::MainWindow(
        QStringLiteral("contentWindow"),
        contentWindowOptions,
        this);
#else
    content_window = new QMainWindow(this);
#endif
    content_window->setObjectName(QStringLiteral("contentWindow"));
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
    content_window->setDockOptions(QMainWindow::AllowTabbedDocks |
                                   QMainWindow::AllowNestedDocks |
                                   QMainWindow::AnimatedDocks |
                                   QMainWindow::GroupedDragging);
#endif

    // Use an invisible placeholder as central widget so docking keeps a stable center area.
    // On KDD we keep a small minimum so newly placed docks are not forced to consume
    // all available space around a collapsed center.
    auto *placeholder = new QWidget(content_window);
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    placeholder->setMinimumSize(220, 160);
    placeholder->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
#else
    placeholder->setFixedSize(0, 10);
#endif
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (auto *kdd = dynamic_cast<KDDockWidgets::QtWidgets::MainWindow *>(content_window))
        kdd->setPersistentCentralWidget(placeholder);
#else
    content_window->setCentralWidget(placeholder);
#endif
    ui->mainLayout->addWidget(content_window);

    // Extract LCDWidget from ui->frame into its own dock
    {
        m_dock_lcd = createMainDock(tr("Screen"),
                                    ui->lcdView,
                                    QStringLiteral("dockLCD"),
                                    Qt::RightDockWidgetArea,
                                    nullptr,
                                    QIcon(),
                                    true,
                                    false,
                                    true);
        connect(ui->lcdView, &LCDWidget::scaleChanged, this, [this](int percent) {
            m_dock_lcd->setWindowTitle(tr("Screen") + QStringLiteral(" (%1%)").arg(percent));
        });
    }

    // Extract control buttons from ui->frame into their own dock
    {
        auto *controlsWidget = new AdaptiveControlsWidget(content_window);
        controlsWidget->setMinimumHeight(0);
        controlsWidget->setMinimumWidth(120);

        // Reparent buttons from the .ui frame into this new widget
        controlsWidget->addControl(ui->buttonPlayPause);
        controlsWidget->addControl(ui->buttonReset);
        controlsWidget->addControl(ui->buttonScreenshot);
        controlsWidget->addControl(ui->buttonUSB);
        controlsWidget->addControl(ui->buttonSpeed);

        // Debug toggle button
        {
            auto *debugBtn = new QToolButton(controlsWidget);
            debugBtn->setAutoRaise(true);
            debugBtn->setIconSize(QSize(24, 24));
            debugBtn->setCheckable(true);
            applyMaterialGlyph(debugBtn, 0xE868, tr("Enter debugger"));
            controlsWidget->addControl(debugBtn);
            debugger_toggle_button = debugBtn;
            debugBtn->setEnabled(ui->actionDebugger->isEnabled());
            connect(ui->actionDebugger, &QAction::changed, debugBtn, [this, debugBtn]() {
                debugBtn->setEnabled(ui->actionDebugger->isEnabled());
            });
            connect(debugBtn, &QToolButton::clicked, this, [this]() {
                if (!debugger_active) {
                    ui->actionDebugger->trigger();
                } else {
                    debugStr(QStringLiteral("> c\n"));
                    emit debuggerCommand(QStringLiteral("c"));
                    setDebuggerActive(false);
                }
            });
        }

        m_dock_controls = createMainDock(tr("Controls"),
                                         controlsWidget,
                                         QStringLiteral("dockControls"),
                                         Qt::RightDockWidgetArea,
                                         nullptr,
                                         QIcon(),
                                         true,
                                         false,
                                         true);
        if (m_dock_controls)
            m_dock_controls->setMinimumSize(QSize(0, 0));
    }

    // Hide the now-empty frame (cannot delete -- owned by Ui::MainWindow)
    ui->frame->setVisible(false);

    // Turn the header bar into a fixed toolbar that lives above the dock/central area,
    // similar to VS Code's in-window title / command bar.
    if (ui->headerBar)
    {
        // Detach from the central layout so QMainWindow can manage it as a toolbar
        ui->mainLayout->removeWidget(ui->headerBar);
        ui->headerBar->setParent(nullptr);

        auto *headerToolBar = new QToolBar(this);
        headerToolBar->setObjectName(QStringLiteral("headerToolBar"));
        headerToolBar->setMovable(false);
        headerToolBar->setFloatable(false);
        headerToolBar->setAllowedAreas(Qt::TopToolBarArea);
        headerToolBar->setIconSize(QSize(16, 16));
        headerToolBar->setContentsMargins(0, 0, 0, 0);

        auto *headerAction = new QWidgetAction(headerToolBar);
        headerAction->setDefaultWidget(ui->headerBar);
        headerToolBar->addAction(headerAction);

        addToolBar(Qt::TopToolBarArea, headerToolBar);

#ifdef Q_OS_MAC
        // Hide custom header on macOS; native title bar provides title and traffic lights
        headerToolBar->setVisible(false);
#endif
    }

    // The outer MainWindow no longer hosts docks directly; keep it frameless/themed only.
    setDockOptions(QMainWindow::DockOptions());
    setUnifiedTitleAndToolBarOnMac(false);

    // VS Code-style: bottom panel tabs at top, right panel tabs at top
    content_window->setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    content_window->setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);

    applyWidgetTheme();

    if (ui->statusBar)
    {
    status_bar_tray = new QWidget(ui->statusBar);
    auto *statusLayout = new QHBoxLayout(status_bar_tray);
    statusLayout->setContentsMargins(6, 0, 6, 0);
    statusLayout->setSpacing(6);

    status_label.setContentsMargins(0, 0, 0, 0);
    statusLayout->addWidget(&status_label, 0, Qt::AlignVCenter);

    statusLayout->addStretch(1);

    status_bar_debug_label = new QLabel(status_bar_tray);
    status_bar_debug_label->setObjectName(QStringLiteral("statusDebugLabel"));
    status_bar_debug_label->setContentsMargins(0, 0, 0, 0);
    status_bar_debug_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    status_bar_debug_label->setVisible(false);
    statusLayout->addWidget(status_bar_debug_label, 0, Qt::AlignVCenter);

    status_bar_speed_label = new QLabel(status_bar_tray);
    status_bar_speed_label->setObjectName(QStringLiteral("statusSpeedLabel"));
    status_bar_speed_label->setContentsMargins(0, 0, 0, 0);
    status_bar_speed_label->setMinimumWidth(90);
    status_bar_speed_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    status_bar_speed_label->setText(tr("Speed: -- %"));
    statusLayout->addWidget(status_bar_speed_label, 0, Qt::AlignVCenter);

    status_dark_button = new QToolButton(status_bar_tray);
    status_dark_button->setObjectName(QStringLiteral("statusDarkModeButton"));
    status_dark_button->setCheckable(false);
    status_dark_button->setAutoRaise(true);
    status_dark_button->setFocusPolicy(Qt::NoFocus);
    status_dark_button->setContentsMargins(0, 0, 0, 0);
    status_dark_button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    const int sbHeight = ui->statusBar->sizeHint().height();
    status_dark_button->setFixedHeight(sbHeight - 2);
    status_dark_button->setMinimumWidth(sbHeight - 2);
    statusLayout->addWidget(status_dark_button, 0, Qt::AlignVCenter);

    ui->statusBar->addPermanentWidget(status_bar_tray, 1);
    }

    // Register QtKeypadBridge for the virtual keyboard functionality
    ui->keypadWidget->installEventFilter(&qt_keypad_bridge);
    ui->lcdView->installEventFilter(&qt_keypad_bridge);
    lcd.installEventFilter(&qt_keypad_bridge);

    ui->keypadWidget->setAttribute(Qt::WA_AcceptTouchEvents);

    qml_engine = ui->keypadWidget->engine();
    qml_engine->addImportPath(QStringLiteral("qrc:/qml/qml"));
    ui->keypadWidget->setSource(QUrl(QStringLiteral("qrc:/qml/qml/ScrollingKeypad.qml")));

    // Create config dialog component
    config_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/FBConfigDialog.qml")), this);
    if (!config_component->isReady())
        qCritical() << "Could not create QML config dialog:" << config_component->errorString();

    // Create flash dialog UI component
    flash_dialog_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/FlashDialog.qml")), this);
    if (!flash_dialog_component->isReady())
        qCritical() << "Could not create flash dialog component:" << flash_dialog_component->errorString();

    // Create mobile UI component
    mobileui_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/MobileUI.qml")), this);
    if (!mobileui_component->isReady())
        qCritical() << "Could not create mobile UI component:" << mobileui_component->errorString();

    if (!qmlBridge())
        throw std::runtime_error("Can't continue without QMLBridge");

    QAction *darkAction = findChild<QAction *>(QStringLiteral("actionDarkMode"));
    if (!darkAction && ui->menuTools)
    {
        darkAction = new QAction(tr("Dark mode"), this);
        darkAction->setObjectName(QStringLiteral("actionDarkMode"));
        darkAction->setCheckable(true);
        if (ui->menuLanguage)
            ui->menuTools->insertAction(ui->menuLanguage->menuAction(), darkAction);
        else
            ui->menuTools->addAction(darkAction);
    }
    const bool darkModeEnabled = qmlBridge()->getDarkTheme();
    if (darkAction)
    {
        darkAction->setChecked(darkModeEnabled);
        connect(darkAction, &QAction::toggled, this, [this](bool darkEnabled)
                { qmlBridge()->setDarkTheme(darkEnabled); });
    }

    if (status_dark_button)
    {
        applyThemeGlyph(status_dark_button, darkModeEnabled);
        connect(status_dark_button, &QToolButton::clicked, this, [this, darkAction]()
                {
            const bool next = !qmlBridge()->getDarkTheme();
            if (darkAction)
                darkAction->setChecked(next);
            else if (qmlBridge())
                qmlBridge()->setDarkTheme(next); });
    }

    connect(qmlBridge(), &QMLBridge::darkThemeChanged, this, [this, darkAction]()
            {
        const bool dark = qmlBridge()->getDarkTheme();
        if (darkAction && darkAction->isChecked() != dark)
            darkAction->setChecked(dark);
        applyWidgetTheme(); });
    if (status_dark_button)
    {
        connect(qmlBridge(), &QMLBridge::darkThemeChanged, status_dark_button, [this, applyThemeGlyph]()
                {
            const bool dark = qmlBridge()->getDarkTheme();
            applyThemeGlyph(status_dark_button, dark); });
    }

    connect(ui->buttonWindowClose, &QToolButton::clicked, this, &QWidget::close);
    connect(ui->buttonWindowMinimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(ui->buttonWindowMaximize, &QToolButton::clicked, this, &QWidget::showMaximized);

#ifdef Q_OS_MAC
    // Hide custom window buttons on macOS; native traffic lights are in the title bar
    ui->buttonWindowClose->setVisible(false);
    ui->buttonWindowMinimize->setVisible(false);
    ui->buttonWindowMaximize->setVisible(false);
#endif

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
