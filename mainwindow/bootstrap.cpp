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
#include "ui/docking/manager/dockmanager.h"
#include "ui/widgets/hwconfig/hwconfigwidget.h"
#include "transfer/usblinktreewidget.h"
#include "ui/screen/framebuffer.h"
#include "ui/input/keypadbridge.h"
#include "ui/theme/materialicons.h"
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

    setupActionAndMenuWiring();
    initializePersistentSettingsAndState();
    finalizeStartupSequence();
}
