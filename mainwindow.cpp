#include <QFileDialog>
#include <QStandardPaths>
#include <QTextBlock>
#include <QMessageBox>
#include <QGraphicsItem>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFrame>
#include <QMenuBar>
#include <QLabel>
#include <QActionGroup>
#include <QMimeData>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QShortcut>
#include <QTimer>
#include <QKeySequence>
#include <QProcess>
#include <QQmlComponent>
#include <QToolBar>
#include <QWidgetAction>
#include <QIcon>
#include <QToolButton>
#include <QRegularExpression>
#include <QHeaderView>
#include <QColor>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QApplication>
#include <QPalette>
#include <QAbstractItemView>
#include <QEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QUrl>
#include <QFontDatabase>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>

#include <array>
#include <utility>

#include "core/debug.h"
#include "core/debug_api.h"
#include "core/emu.h"
#include "core/flash.h"
#include "core/gif.h"
#include "core/misc.h"
#include "core/mem.h"
#include "core/usblink_queue.h"

#include "ui/dockwidget.h"
#include "ui/kdockwidget.h"
#include "mainwindow.h"
#include "ui/widgettheme.h"
#include "ui/materialicons.h"
#include "ui_mainwindow.h"
#include "app/qmlbridge.h"
#include "ui/framebuffer.h"
#include "ui/keypadbridge.h"
#include "debugger/dockmanager.h"

#include "debugger/disassembly/disassemblywidget.h"
#include "debugger/hexview/hexviewwidget.h"
#include "debugger/console/consolewidget.h"
#include "debugger/nandbrowser/nandbrowserwidget.h"
#include "debugger/hwconfig/hwconfigwidget.h"

MainWindow *main_window;

MainWindow *getMainWindow()
{
    return main_window;
}

void setMainWindow(MainWindow *window)
{
    main_window = window;
}
// Only bump this for incompatible structural changes (e.g. nested QMainWindow
// redesign).  Adding/removing individual docks does NOT require a bump --
// restoreState() gracefully skips missing docks and leaves new ones at their
// default positions.
static const constexpr int WindowStateVersion = 9;

enum class MainDockId {
    LCD,
    ExternalLCD,
    Controls,
    NandBrowser,
    HwConfig,
};

static const char *mainDockObjectName(MainDockId id)
{
    switch (id) {
    case MainDockId::LCD:         return "dockLCD";
    case MainDockId::ExternalLCD: return "dockExternalLCD";
    case MainDockId::Controls:    return "dockControls";
    case MainDockId::NandBrowser: return "dockNandBrowser";
    case MainDockId::HwConfig:    return "dockHwConfig";
    }
    return "dockUnknown";
}

static constexpr const char *kSettingHwBatteryOverride = "hwBatteryOverride";
static constexpr const char *kSettingHwChargingOverride = "hwChargingOverride";
static constexpr const char *kSettingHwBrightnessOverride = "hwBrightnessOverride";
static constexpr const char *kSettingHwKeypadTypeOverride = "hwKeypadTypeOverride";
static constexpr const char *kSettingHwBatteryMvOverride = "hwBatteryMvOverride";
static constexpr const char *kSettingHwChargerStateOverride = "hwChargerStateOverride";
static constexpr const char *kSettingWindowLayoutJson = "windowLayoutJson";
static constexpr const char *kSettingLayoutProfile = "layoutProfile";
static constexpr const char *kSettingDebugDockStateJson = "debugDockStateJson";
static constexpr const char *kSettingDockFocusPolicy = "dockFocusPolicy";
static constexpr const char *kLayoutSchemaQMainWindowV1 = "firebird.qmainwindow.layout.v1";
static constexpr int kMaxLayoutHistoryEntries = 10;
static constexpr const char *kSettingLayoutMigrationNoticeShown = "layoutMigrationNoticeShown";

struct HwOverrides {
    int batteryRaw = -1;
    int charging = -1;
    int brightness = -1;
    int keypadType = -1;
    int batteryMv = -1;
    int chargerState = -1;
};

static HwOverrides readHwOverridesFromSettings(QSettings *settings)
{
    HwOverrides overrides;
    if (!settings)
        return overrides;
    const auto readValue = [settings](const char *key) {
        return settings->value(QString::fromLatin1(key), -1).toInt();
    };
    overrides.batteryRaw = readValue(kSettingHwBatteryOverride);
    overrides.charging = readValue(kSettingHwChargingOverride);
    overrides.brightness = readValue(kSettingHwBrightnessOverride);
    overrides.keypadType = readValue(kSettingHwKeypadTypeOverride);
    overrides.batteryMv = readValue(kSettingHwBatteryMvOverride);
    overrides.chargerState = readValue(kSettingHwChargerStateOverride);
    return overrides;
}

static void writeHwOverridesToSettings(QSettings *settings, const HwOverrides &overrides)
{
    if (!settings)
        return;
    const std::array<std::pair<const char *, int>, 6> values = {{
        { kSettingHwBatteryOverride, overrides.batteryRaw },
        { kSettingHwChargingOverride, overrides.charging },
        { kSettingHwBrightnessOverride, overrides.brightness },
        { kSettingHwKeypadTypeOverride, overrides.keypadType },
        { kSettingHwBatteryMvOverride, overrides.batteryMv },
        { kSettingHwChargerStateOverride, overrides.chargerState },
    }};
    for (const auto &entry : values)
        settings->setValue(QString::fromLatin1(entry.first), entry.second);
}

static QString dockAreaToString(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return QStringLiteral("left");
    case Qt::RightDockWidgetArea: return QStringLiteral("right");
    case Qt::TopDockWidgetArea: return QStringLiteral("top");
    case Qt::BottomDockWidgetArea: return QStringLiteral("bottom");
    default: break;
    }
    return QStringLiteral("none");
}

static QJsonObject exportLegacyDockLayoutJson(QMainWindow *window, const QByteArray &state, int version)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kLayoutSchemaQMainWindowV1));
    root.insert(QStringLiteral("windowStateVersion"), version);
    root.insert(QStringLiteral("windowStateBase64"), QString::fromLatin1(state.toBase64()));

    QJsonArray docks;
    if (window) {
        const auto dockChildren = window->findChildren<QDockWidget *>();
        for (QDockWidget *dw : dockChildren) {
            if (!dw)
                continue;
            QJsonObject dock;
            dock.insert(QStringLiteral("objectName"), dw->objectName());
            dock.insert(QStringLiteral("title"), dw->windowTitle());
            dock.insert(QStringLiteral("visible"), dw->isVisible());
            dock.insert(QStringLiteral("floating"), dw->isFloating());
            dock.insert(QStringLiteral("area"), dockAreaToString(window->dockWidgetArea(dw)));
            dock.insert(QStringLiteral("geometryBase64"),
                        QString::fromLatin1(dw->saveGeometry().toBase64()));
            docks.append(dock);
        }
    }
    root.insert(QStringLiteral("docks"), docks);
    return root;
}

static bool extractWindowStateFromLayoutObject(const QJsonObject &root,
                                               QByteArray *stateOut,
                                               int *versionOut,
                                               QString *errorOut = nullptr)
{
    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (!schema.isEmpty() && schema != QLatin1String(kLayoutSchemaQMainWindowV1)) {
        if (errorOut)
            *errorOut = QStringLiteral("unsupported layout schema: %1").arg(schema);
        return false;
    }

    const QString stateBase64 = root.value(QStringLiteral("windowStateBase64")).toString();
    if (stateBase64.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("windowStateBase64 missing");
        return false;
    }

    const QByteArray state = QByteArray::fromBase64(stateBase64.toLatin1());
    if (state.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("windowStateBase64 decode failed");
        return false;
    }

    if (stateOut)
        *stateOut = state;
    if (versionOut)
        *versionOut = root.value(QStringLiteral("windowStateVersion")).toInt(WindowStateVersion);
    return true;
}

static QString layoutProfilesDirPath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty())
        return QString();
    return configDir + QStringLiteral("/layouts");
}

static QString layoutProfilePath(const QString &profileName)
{
    return layoutProfilesDirPath() + QLatin1Char('/') + profileName + QStringLiteral(".json");
}

static bool ensureLayoutProfilesDir(QString *errorOut = nullptr)
{
    const QString dirPath = layoutProfilesDirPath();
    if (dirPath.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("layout profile config directory is unavailable");
        return false;
    }
    QDir dir(dirPath);
    if (dir.exists())
        return true;
    if (QDir().mkpath(dirPath))
        return true;
    if (errorOut)
        *errorOut = QStringLiteral("could not create profile directory: %1").arg(dirPath);
    return false;
}

static bool saveLayoutProfile(QMainWindow *window,
                              const QString &profileName,
                              int version,
                              const QJsonObject &debugDockState = QJsonObject(),
                              QString *errorOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }
    if (!ensureLayoutProfilesDir(errorOut))
        return false;

    const QByteArray state = window->saveState(version);
    QJsonObject layoutJson = exportLegacyDockLayoutJson(window, state, version);
    if (!debugDockState.isEmpty())
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
    const QJsonDocument doc(layoutJson);

    const QString filePath = layoutProfilePath(profileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("could not open %1 for write").arg(filePath);
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

static bool restoreLayoutProfile(QMainWindow *window, const QString &profileName, int fallbackVersion,
                                 QString *errorOut = nullptr,
                                 QJsonObject *debugDockStateOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }

    const QString filePath = layoutProfilePath(profileName);
    QFile file(filePath);
    if (!file.exists()) {
        if (errorOut)
            *errorOut = QStringLiteral("profile does not exist: %1").arg(filePath);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("could not open %1 for read").arg(filePath);
        return false;
    }

    QJsonParseError jsonParseError = {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonParseError);
    file.close();
    if (jsonParseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("invalid JSON in %1").arg(filePath);
        return false;
    }

    const QJsonObject root = doc.object();
    if (debugDockStateOut)
        *debugDockStateOut = root.value(QStringLiteral("debugDockState")).toObject();

    QByteArray state;
    int profileVersion = fallbackVersion;
    QString stateParseError;
    if (!extractWindowStateFromLayoutObject(root, &state, &profileVersion, &stateParseError)) {
        if (errorOut)
            *errorOut = QStringLiteral("%1 in %2").arg(stateParseError, filePath);
        return false;
    }

    for (int version = profileVersion; version >= 1; --version) {
        if (window->restoreState(state, version))
            return true;
    }

    if (errorOut)
        *errorOut = QStringLiteral("restoreState failed for all versions in %1").arg(filePath);
    return false;
}

/* WidgetTheme, applyPaletteColors, setWidgetBackground now in widgettheme.h/cpp */

void MainWindow::applyStandardDockFeatures(QDockWidget *dw) const
{
    if (!dw)
        return;
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    dw->setFeatures(QDockWidget::DockWidgetClosable |
                    QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
}

DockWidget *MainWindow::createMainDock(const QString &title,
                                       QWidget *widget,
                                       const QString &objectName,
                                       Qt::DockWidgetArea area,
                                       QMenu *docksMenu,
                                       const QIcon &icon,
                                       bool hideTitlebar)
{
    auto *dw = new KDockWidget(title, content_window);
    if (hideTitlebar)
        dw->applyThinTitlebar(true);
    if (!objectName.isEmpty())
        dw->setObjectName(objectName);
    if (!icon.isNull())
        dw->setWindowIcon(icon);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw);
    content_window->addDockWidget(area, dw);
    if (docksMenu) {
        QAction *action = dw->toggleViewAction();
        if (!icon.isNull())
            action->setIcon(icon);
        docksMenu->addAction(action);
    }
    return dw;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (event)
        QMainWindow::resizeEvent(event);
#ifdef Q_OS_MAC
    if (!isFullScreen())
    {
        // Apply rounded corners to the frameless window on macOS
        const int radius = 12;
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, width(), height()), radius, radius);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
#endif
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow)
{
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
    ui->buttonSpeed->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->buttonSpeed->setFixedSize(controlSize);
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
    connect(&emu_thread, &EmuThread::paused, this, [this](bool)
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });

    // Create an inner QMainWindow that will host all docks and the LCD frame.
    // This lets the custom header bar sit above everything else (similar to VS Code),
    // while docks live around the central emulator surface without overlapping the header.
    content_window = new QMainWindow(this);
    content_window->setObjectName(QStringLiteral("contentWindow"));
    content_window->setDockOptions(QMainWindow::AllowTabbedDocks |
                                   QMainWindow::AllowNestedDocks |
                                   QMainWindow::AnimatedDocks |
                                   QMainWindow::GroupedDragging);

    // Use an invisible placeholder as central widget so docks fill all the space.
    // setFixedSize(0, 10) keeps a minimal vertical extent so all four dock areas
    // remain usable; setMaximumSize(0,0) can cause Qt to collapse adjacent docks.
    auto *placeholder = new QWidget(content_window);
    placeholder->setFixedSize(0, 10);
    content_window->setCentralWidget(placeholder);
    ui->mainLayout->addWidget(content_window);

    // Extract LCDWidget from ui->frame into its own dock
    {
        m_dock_lcd = createMainDock(tr("Screen"),
                                    ui->lcdView,
                                    QString::fromLatin1(mainDockObjectName(MainDockId::LCD)),
                                    Qt::RightDockWidgetArea);
        connect(ui->lcdView, &LCDWidget::scaleChanged, this, [this](int percent) {
            m_dock_lcd->setWindowTitle(tr("Screen") + QStringLiteral(" (%1%)").arg(percent));
        });
    }

    // Extract control buttons from ui->frame into their own dock
    {
        auto *controlsWidget = new QWidget(content_window);
        controlsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *controlsLayout = new QHBoxLayout(controlsWidget);
        controlsLayout->setContentsMargins(4, 2, 4, 2);
        controlsLayout->setSpacing(8);
        controlsLayout->setAlignment(Qt::AlignHCenter);

        // Reparent buttons from the .ui frame into this new widget
        controlsLayout->addWidget(ui->buttonPlayPause);
        controlsLayout->addWidget(ui->buttonReset);
        controlsLayout->addWidget(ui->buttonScreenshot);
        controlsLayout->addWidget(ui->buttonUSB);
        controlsLayout->addWidget(ui->buttonSpeed);

        // Debug toggle button
        {
            auto *debugBtn = new QToolButton(controlsWidget);
            debugBtn->setAutoRaise(true);
            debugBtn->setIconSize(QSize(24, 24));
            debugBtn->setCheckable(true);
            applyMaterialGlyph(debugBtn, 0xE868, tr("Enter debugger"));
            controlsLayout->addWidget(debugBtn);
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
                                         QString::fromLatin1(mainDockObjectName(MainDockId::Controls)),
                                         Qt::RightDockWidgetArea);
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

    if (!the_qml_bridge)
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
    const bool darkModeEnabled = the_qml_bridge->getDarkTheme();
    if (darkAction)
    {
        darkAction->setChecked(darkModeEnabled);
        connect(darkAction, &QAction::toggled, this, [](bool darkEnabled)
                { the_qml_bridge->setDarkTheme(darkEnabled); });
    }

    if (status_dark_button)
    {
        applyThemeGlyph(status_dark_button, darkModeEnabled);
        status_dark_button->setStyleSheet(QStringLiteral(
            "QToolButton { border: 0px; background: transparent; padding: 0 6px; outline: 0px; }"
            "QToolButton:hover { background: transparent; }"
            "QToolButton:pressed { background: transparent; }"
            "QToolButton:focus { outline: 0px; }"));
        connect(status_dark_button, &QToolButton::clicked, this, [darkAction]()
                {
            const bool next = !the_qml_bridge->getDarkTheme();
            if (darkAction)
                darkAction->setChecked(next);
            else if (the_qml_bridge)
                the_qml_bridge->setDarkTheme(next); });
    }

    connect(the_qml_bridge, &QMLBridge::darkThemeChanged, this, [this, darkAction]()
            {
        const bool dark = the_qml_bridge->getDarkTheme();
        if (darkAction && darkAction->isChecked() != dark)
            darkAction->setChecked(dark);
        applyWidgetTheme(); });
    if (status_dark_button)
    {
        connect(the_qml_bridge, &QMLBridge::darkThemeChanged, status_dark_button, [this, applyThemeGlyph]()
                {
            const bool dark = the_qml_bridge->getDarkTheme();
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
    connect(&emu_thread, SIGNAL(serialChar(char)), this, SLOT(serialChar(char)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debugStr(QString)), this, SLOT(debugStr(QString)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(nlogStr(QString)), this, SLOT(nlogStr(QString)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(isBusy(bool)), this, SLOT(isBusy(bool)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(statusMsg(QString)), this, SLOT(showStatusMsg(QString)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debugInputRequested(bool)), this, SLOT(debugInputRequested(bool)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debuggerEntered(bool)), this, SLOT(debuggerEntered(bool)), Qt::QueuedConnection);

    // GUI -> Emu (no QueuedConnection possible, watch out!)
    connect(this, SIGNAL(debuggerCommand(QString)), &emu_thread, SLOT(debuggerInput(QString)));

    // Menu "Emulator"
    connect(ui->buttonReset, SIGNAL(clicked(bool)), &emu_thread, SLOT(reset()));
    connect(ui->actionReset, SIGNAL(triggered()), &emu_thread, SLOT(reset()));
    connect(ui->actionRestart, SIGNAL(triggered()), this, SLOT(restart()));
    connect(ui->actionDebugger, SIGNAL(triggered()), &emu_thread, SLOT(enterDebugger()));
    connect(ui->actionLaunch_IDA, SIGNAL(triggered()), this, SLOT(launchIdaInstantDebugging()));
    if (ui->actionLaunch_IDA) {
        ui->actionLaunch_IDA->setToolTip(tr("Experimental: launch IDA and attach to Firebird GDB server"));
        ui->actionLaunch_IDA->setStatusTip(tr("Experimental feature; not covered by automated tests."));
    }
    connect(ui->actionConfiguration, SIGNAL(triggered()), this, SLOT(openConfiguration()));
    connect(ui->actionPause, SIGNAL(toggled(bool)), &emu_thread, SLOT(setPaused(bool)));
    connect(ui->buttonSpeed, SIGNAL(clicked(bool)), &emu_thread, SLOT(setTurboMode(bool)));

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
    connect(ui->buttonScreenshot, SIGNAL(clicked()), this, SLOT(screenshot()));
    connect(ui->actionScreenshot, SIGNAL(triggered()), this, SLOT(screenshot()));
    ui->actionScreenshot->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    {
        auto *saveScreenshotAction = new QAction(tr("Save Screenshot..."), this);
        saveScreenshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
        connect(saveScreenshotAction, &QAction::triggered, this, &MainWindow::screenshotToFile);
        ui->menuTools->insertAction(ui->actionRecord_GIF, saveScreenshotAction);
    }
    connect(ui->actionRecord_GIF, SIGNAL(triggered()), this, SLOT(recordGIF()));
    connect(ui->actionConnect, SIGNAL(triggered()), this, SLOT(connectUSB()));
    connect(ui->buttonUSB, SIGNAL(clicked(bool)), this, SLOT(connectUSB()));
    connect(ui->actionLCD_Window, SIGNAL(triggered(bool)), this, SLOT(setExtLCD(bool)));
    connect(ui->actionXModem, SIGNAL(triggered()), this, SLOT(xmodemSend()));
    connect(ui->actionSwitch_to_Mobile_UI, SIGNAL(triggered()), this, SLOT(switchToMobileUI()));
    connect(ui->actionLeavePTT, &QAction::triggered, the_qml_bridge, &QMLBridge::sendExitPTT);
    ui->actionConnect->setShortcut(QKeySequence(Qt::Key_F10));
    ui->actionConnect->setAutoRepeat(false);

    // Menu "State"
    connect(ui->actionResume, SIGNAL(triggered()), this, SLOT(resume()));
    connect(ui->actionSuspend, SIGNAL(triggered()), this, SLOT(suspend()));
    connect(ui->actionResume_from_file, SIGNAL(triggered()), this, SLOT(resumeFromFile()));
    connect(ui->actionSuspend_to_file, SIGNAL(triggered()), this, SLOT(suspendToFile()));

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
    connect(ui->actionSave, SIGNAL(triggered()), this, SLOT(saveFlash()));
    connect(ui->actionCreate_flash, SIGNAL(triggered()), this, SLOT(createFlash()));

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
    connect(ui->actionAbout_Firebird, SIGNAL(triggered(bool)), this, SLOT(showAbout()));
    connect(ui->actionAbout_Qt, SIGNAL(triggered(bool)), qApp, SLOT(aboutQt()));

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
    connect(ui->refreshButton, SIGNAL(clicked(bool)), ui->usblinkTree, SLOT(reloadFilebrowser()));
    connect(ui->usblinkTree, SIGNAL(downloadProgress(int)), this, SLOT(usblinkDownload(int)), Qt::QueuedConnection);
    connect(ui->usblinkTree, SIGNAL(uploadProgress(int)), this, SLOT(changeProgress(int)), Qt::QueuedConnection);
    connect(this, SIGNAL(usblink_progress_changed(int)), this, SLOT(changeProgress(int)), Qt::QueuedConnection);

    // QMLBridge
    KitModel *model = the_qml_bridge->getKitModel();
    connect(model, SIGNAL(anythingChanged()), this, SLOT(kitAnythingChanged()));
    connect(model, SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)), this, SLOT(kitDataChanged(QModelIndex, QModelIndex, QVector<int>)));
    connect(the_qml_bridge, SIGNAL(currentKitChanged(const Kit &)), this, SLOT(currentKitChanged(const Kit &)));

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

    /* Dock/window initialization order is significant:
     * 1) create all main/debug docks (including dynamic extra hex docks),
     * 2) restore geometry/window state against those concrete dock objects,
     * 3) apply post-restore links/theme behavior.
     * Reordering these steps can silently break layout restoration. */
    convertTabsToDocks();
    if (m_debugDocks) {
        const int extraHexDocks = qMax(0, settings->value(QStringLiteral("debugExtraHexDockCount"), 0).toInt());
        m_debugDocks->ensureExtraHexDocks(extraHexDocks);
    }
    retranslateDocks();
    if (m_dock_ext_lcd)
        m_dock_ext_lcd->restoreGeometry(settings->value(QStringLiteral("extLCDGeometry")).toByteArray());
    setExtLCD(settings->value(QStringLiteral("extLCDVisible")).toBool());
    restoreGeometry(settings->value(QStringLiteral("windowGeometry")).toByteArray());

    // Restore dock layout.  Try the current version first; on version mismatch
    // attempt older versions so the user's layout survives dock additions/removals.
    // Only fall back to the default layout if nothing works at all.
    QByteArray savedState = settings->value(QStringLiteral("windowState")).toByteArray();
    bool restored = false;
    bool restoredFromLegacyWindowState = false;
    bool restoredFromLegacyLayoutJson = false;
    const QString startupProfile = settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed();
    QJsonObject restoredDebugDockState;
    const QString autoProfile = startupProfile.isEmpty() ? QStringLiteral("default") : startupProfile;
    if (!autoProfile.isEmpty()) {
        QString profileError;
        if (restoreLayoutProfile(content_window, autoProfile, WindowStateVersion,
                                 &profileError, &restoredDebugDockState)) {
            restored = true;
        } else if (!startupProfile.isEmpty()) {
            qDebug("profile restore failed (%s): %s",
                   autoProfile.toUtf8().constData(),
                   profileError.toUtf8().constData());
        }
    }
    if (!restored && !savedState.isEmpty()) {
        for (int v = WindowStateVersion; v >= 1 && !restored; --v)
            restored = content_window->restoreState(savedState, v);
        if (restored)
            restoredFromLegacyWindowState = true;
    }
    if (!restored) {
        const QString layoutJsonText = settings->value(QString::fromLatin1(kSettingWindowLayoutJson)).toString();
        if (!layoutJsonText.isEmpty()) {
            QJsonParseError parseError = {};
            const QJsonDocument doc = QJsonDocument::fromJson(layoutJsonText.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QByteArray stateFromJson;
                int jsonVersion = WindowStateVersion;
                if (extractWindowStateFromLayoutObject(doc.object(), &stateFromJson, &jsonVersion)) {
                    for (int v = jsonVersion; v >= 1 && !restored; --v)
                        restored = content_window->restoreState(stateFromJson, v);
                    if (restoredDebugDockState.isEmpty())
                        restoredDebugDockState = doc.object().value(QStringLiteral("debugDockState")).toObject();
                    if (restored)
                        restoredFromLegacyLayoutJson = true;
                }
            }
        }
    }
    if (!restored) {
        qDebug("restoreState failed (size=%lld) -- applying default layout",
               (long long)savedState.size());
        resetDockLayout();
    }

    if (m_debugDocks) {
        if (!restoredDebugDockState.isEmpty()) {
            m_debugDocks->restoreDockStates(restoredDebugDockState);
        } else {
            QJsonParseError debugStateErr = {};
            const QString savedDebugState = settings->value(QString::fromLatin1(kSettingDebugDockStateJson)).toString();
            const QJsonDocument debugStateDoc = QJsonDocument::fromJson(savedDebugState.toUtf8(), &debugStateErr);
            if (debugStateErr.error == QJsonParseError::NoError && debugStateDoc.isObject())
                m_debugDocks->restoreDockStates(debugStateDoc.object());
        }
    }

    if (!startupProfile.isEmpty() || restored) {
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), autoProfile);
    }
    if (restoredFromLegacyWindowState || restoredFromLegacyLayoutJson) {
        const QJsonObject debugDockState = m_debugDocks ? m_debugDocks->serializeDockStates()
                                                        : QJsonObject();
        QString migrateError;
        bool migrated = false;
        if (!saveLayoutProfile(content_window, autoProfile, WindowStateVersion, debugDockState, &migrateError)) {
            qDebug("legacy layout migration to profile '%s' failed: %s",
                   autoProfile.toUtf8().constData(),
                   migrateError.toUtf8().constData());
        } else {
            migrated = true;
            qDebug("migrated legacy layout to profile '%s'", autoProfile.toUtf8().constData());
        }

        QString backupPath;
        QString backupJson = settings->value(QString::fromLatin1(kSettingWindowLayoutJson)).toString();
        if (backupJson.isEmpty()) {
            const QByteArray currentState = content_window->saveState(WindowStateVersion);
            const QJsonObject backupObj = exportLegacyDockLayoutJson(content_window, currentState, WindowStateVersion);
            backupJson = QString::fromUtf8(QJsonDocument(backupObj).toJson(QJsonDocument::Indented));
        }
        if (!backupJson.isEmpty()) {
            QString dirError;
            if (ensureLayoutProfilesDir(&dirError)) {
                backupPath = layoutProfilesDirPath() + QStringLiteral("/layouts.bak.json");
                QFile backupFile(backupPath);
                if (backupFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    backupFile.write(backupJson.toUtf8());
                    backupFile.close();
                } else {
                    qDebug("could not write legacy layout backup: %s",
                           backupPath.toUtf8().constData());
                    backupPath.clear();
                }
            }
        }

        if (migrated && !settings->value(QString::fromLatin1(kSettingLayoutMigrationNoticeShown), false).toBool()) {
            settings->setValue(QString::fromLatin1(kSettingLayoutMigrationNoticeShown), true);
            const QString noticePath = backupPath;
            QTimer::singleShot(0, this, [this, noticePath]() {
                const QString msg = noticePath.isEmpty()
                        ? tr("Layout format updated. Your layout now uses JSON profiles.")
                        : tr("Layout format updated. Legacy layout backup saved to:\n%1")
                              .arg(noticePath);
                QMessageBox::information(this, tr("Layout Migration"), msg);
            });
        }
    }

    m_layoutUndoHistory.clear();
    m_layoutRedoHistory.clear();
    captureLayoutHistorySnapshot();

    m_lcdKeypadLinked = settings->value(QStringLiteral("lcdKeypadLinked"), false).toBool();

    // Restore HW config overrides
    const HwOverrides hw = readHwOverridesFromSettings(settings);
    hw_override_set_adc_battery_level(static_cast<int16_t>(hw.batteryRaw));
    hw_override_set_adc_charging(static_cast<int8_t>(hw.charging));
    hw_override_set_lcd_contrast(static_cast<int16_t>(hw.brightness));
    hw_override_set_adc_keypad_type(static_cast<int16_t>(hw.keypadType));
    hw_override_set_battery_mv(hw.batteryMv);
    int savedChargerState = hw.chargerState;
    if (savedChargerState >= CHARGER_DISCONNECTED && savedChargerState <= CHARGER_CHARGING)
        hw_override_set_charger_state((charger_state_t)savedChargerState);
    else if (hw_override_get_adc_charging() >= 0)
        hw_override_set_charger_state(hw_override_get_adc_charging() ? CHARGER_CHARGING : CHARGER_DISCONNECTED);
    else
        hw_override_set_charger_state(CHARGER_AUTO);
    if (m_hwConfig)
        m_hwConfig->syncOverridesFromGlobals();

    refillKitMenus();

    ui->lcdView->setFocus();

    // Ensure dock buttons/theme are refreshed after docks are created.
    applyWidgetTheme();

    // Select default Kit
    bool defaultKitFound = the_qml_bridge->useDefaultKit();

    if (the_qml_bridge->getKitModel()->allKitsEmpty())
    {
        // Do not show the window before MainWindow gets shown,
        // otherwise it won't be in focus.
        QTimer::singleShot(0, this, SIGNAL(openConfiguration()));

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

    if (!the_qml_bridge->getAutostart())
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
        if (!the_qml_bridge->getSnapshotPath().isEmpty())
            resumed = resume();

        if (!resumed)
        {
            // Boot up normally
            if (!emu_thread.boot1.isEmpty() && !emu_thread.flash.isEmpty())
                restart();
            else
                showStatusMsg(tr("Start the emulation via Emulation->Start."));
        }
    }
}

void MainWindow::applyWidgetTheme()
{
    const WidgetTheme &theme = currentWidgetTheme();

    /* Fusion is the only Qt style that fully respects qApp->setPalette().
     * The macOS native style ignores palette for most widgets.
     * CEmu uses the same approach. */
    static bool fusionSet = false;
    if (!fusionSet) {
        qApp->setStyle(QStringLiteral("Fusion"));
        fusionSet = true;
    }

    /* Build palette and apply globally. Fusion handles the rest. */
    QPalette pal;
    applyPaletteColors(pal, theme);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, theme.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Text, theme.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, theme.textMuted);
    pal.setColor(QPalette::Mid, theme.border);
    pal.setColor(QPalette::Dark, theme.border);
    pal.setColor(QPalette::Light, theme.surfaceAlt);
    pal.setColor(QPalette::Midlight, theme.surfaceAlt);
    pal.setColor(QPalette::Shadow, theme.window);
    qApp->setPalette(pal);

    /* VS Code-style comprehensive stylesheet */
    if (content_window) {
        QString ss = QStringLiteral(
            /* Tab bar styling (bottom and right dock areas) */
            "QTabBar::tab {"
            "  background: %1;"
            "  color: %2;"
            "  padding: 4px 12px;"
            "  border: none;"
            "  border-bottom: 2px solid transparent;"
            "}"
            "QTabBar::tab:selected {"
            "  color: %3;"
            "  border-bottom: 2px solid %4;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  color: %3;"
            "}"
            /* Thin scrollbars */
            "QScrollBar:vertical {"
            "  width: 10px; background: transparent; margin: 0;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: %5; border-radius: 4px; min-height: 20px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: rgba(128,128,128,140);"
            "}"
            "QScrollBar:horizontal {"
            "  height: 10px; background: transparent; margin: 0;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: %5; border-radius: 4px; min-width: 20px;"
            "}"
            "QScrollBar::handle:horizontal:hover {"
            "  background: rgba(128,128,128,140);"
            "}"
            "QScrollBar::add-line, QScrollBar::sub-line {"
            "  height: 0; width: 0;"
            "}"
            "QScrollBar::add-page, QScrollBar::sub-page {"
            "  background: transparent;"
            "}"
            /* Splitter handle styling */
            "QSplitter::handle {"
            "  background: %6;"
            "}"
            "QSplitter::handle:hover {"
            "  background: %7;"
            "}"
            /* Input field focus border */
            "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {"
            "  border: 1px solid %7;"
            "}"
        ).arg(
            theme.dock.name(),                // %1 tab bg
            theme.panelTabInactiveFg.name(),  // %2 inactive tab text
            theme.panelTabActiveFg.name(),    // %3 active tab text
            theme.panelTabActiveBorder.name(),// %4 active tab border
            theme.scrollbarThumb.name(),      // %5 scrollbar thumb
            theme.border.name(),              // %6 splitter
            theme.accent.name()               // %7 accent
        );
        content_window->setStyleSheet(ss);
    }

    /* The outer QMainWindow has no docks of its own; suppress the Fusion-style
     * separator lines that Qt draws at each dock-area boundary.  Target only
     * the outer window (objectName "MainWindow") so content_window's dock
     * resize handles remain functional. */
    setStyleSheet(QStringLiteral(
        "QMainWindow#MainWindow::separator { width: 0; height: 0; }"
        "QToolBar#headerToolBar { border: none; }"
    ));

    /* Refresh dock icons (color may have changed with theme) and thin title bars */
    if (m_debugDocks)
        m_debugDocks->refreshIcons();
    if (content_window) {
        const auto dockChildren = content_window->findChildren<DockWidget *>();
        for (DockWidget *dw : dockChildren) {
            dw->applyThinBarStyle();
            dw->refreshTitlebar();  // pick up new icon pixmaps
        }
    }

    /* Also refresh menu action icons */
    {
        using namespace MaterialIcons;
        const QColor fg = palette().color(QPalette::WindowText);
        auto mi = [&](ushort cp) { return fromCodepoint(material_icon_font, cp, fg); };

        if (ui->actionRestart)       ui->actionRestart->setIcon(mi(CP::Play));
        if (ui->actionReset)         ui->actionReset->setIcon(mi(CP::Refresh));
        if (ui->actionDebugger)      ui->actionDebugger->setIcon(mi(CP::BugReport));
        if (ui->actionConfiguration) ui->actionConfiguration->setIcon(mi(CP::Settings));
        if (ui->actionPause)         ui->actionPause->setIcon(mi(CP::Pause));
        if (ui->actionScreenshot)    ui->actionScreenshot->setIcon(mi(CP::Screenshot));
        if (ui->actionConnect)       ui->actionConnect->setIcon(mi(CP::USB));
        if (ui->actionRecord_GIF)    ui->actionRecord_GIF->setIcon(mi(CP::Image));
        if (ui->actionLCD_Window)    ui->actionLCD_Window->setIcon(mi(CP::Display));
        if (ui->actionResume)        ui->actionResume->setIcon(mi(CP::Play));
        if (ui->actionSuspend)       ui->actionSuspend->setIcon(mi(CP::Save));
        if (ui->actionSave)          ui->actionSave->setIcon(mi(CP::Save));
        if (ui->actionCreate_flash)  ui->actionCreate_flash->setIcon(mi(CP::Add));
    }

    /* Force repaint on custom-painted widgets (they read theme colors directly) */
    if (m_debugDocks && m_debugDocks->disassembly())
        m_debugDocks->disassembly()->viewport()->update();
    if (m_debugDocks && m_debugDocks->hexView())
        m_debugDocks->hexView()->viewport()->update();
}

MainWindow::~MainWindow()
{
    /* config_dialog and flash_dialog are created by QQmlComponent::create()
     * without a parent, so they must be explicitly deleted.  Use delete
     * instead of deleteLater -- the event loop may not process deferred
     * deletes before the QML engine (a child of this window) is destroyed,
     * which would cause a use-after-free.
     *
     * mobileui_component and the other QQmlComponents are children of this
     * window and will be auto-deleted by ~QWidget(), so we must NOT delete
     * them here (that would double-free). */
    delete mobileui_dialog;
    mobileui_dialog = nullptr;

    delete config_dialog;
    config_dialog = nullptr;

    delete flash_dialog;
    flash_dialog = nullptr;

    savePersistentUiState();
    delete settings;
    delete ui;
}

void MainWindow::savePersistentUiState()
{
    if (!settings)
        return;

    // Save external LCD dock geometry
    settings->setValue(QStringLiteral("extLCDGeometry"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->saveGeometry() : QByteArray());
    settings->setValue(QStringLiteral("extLCDVisible"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->isVisible() : false);

    // Save MainWindow state and geometry
    QByteArray state = content_window->saveState(WindowStateVersion);
    qDebug("Saving windowState: %lld bytes, ver=%d", (long long)state.size(), WindowStateVersion);
    settings->setValue(QStringLiteral("windowState"), state);
    QJsonObject layoutJson = exportLegacyDockLayoutJson(content_window, state, WindowStateVersion);
    QJsonObject debugDockState;
    if (m_debugDocks) {
        debugDockState = m_debugDocks->serializeDockStates();
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
        settings->setValue(QString::fromLatin1(kSettingDebugDockStateJson),
                           QString::fromUtf8(QJsonDocument(debugDockState).toJson(QJsonDocument::Compact)));
    }
    settings->setValue(QString::fromLatin1(kSettingWindowLayoutJson),
                       QString::fromUtf8(QJsonDocument(layoutJson).toJson(QJsonDocument::Compact)));
    settings->setValue(QStringLiteral("windowGeometry"), saveGeometry());
    if (m_debugDocks)
        settings->setValue(QStringLiteral("debugExtraHexDockCount"), m_debugDocks->extraHexDockCount());
    QString activeProfile = settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed();
    if (activeProfile.isEmpty())
        activeProfile = QStringLiteral("default");
    settings->setValue(QString::fromLatin1(kSettingLayoutProfile), activeProfile);
    QString profileError;
    if (!saveLayoutProfile(content_window, activeProfile, WindowStateVersion, debugDockState, &profileError)) {
        qDebug("save layout profile '%s' failed: %s",
               activeProfile.toUtf8().constData(),
               profileError.toUtf8().constData());
    }

    settings->setValue(QStringLiteral("lcdKeypadLinked"), m_lcdKeypadLinked);

    // Save HW config overrides
    const HwOverrides hw = {
        static_cast<int>(hw_override_get_adc_battery_level()),
        static_cast<int>(hw_override_get_adc_charging()),
        static_cast<int>(hw_override_get_lcd_contrast()),
        static_cast<int>(hw_override_get_adc_keypad_type()),
        hw_override_get_battery_mv(),
        static_cast<int>(hw_override_get_charger_state()),
    };
    writeHwOverridesToSettings(settings, hw);

    settings->sync();
}

void MainWindow::scheduleLayoutHistoryCapture()
{
    if (m_layoutHistoryApplying || !m_layoutHistoryTimer)
        return;
    m_layoutHistoryTimer->start();
}

void MainWindow::captureLayoutHistorySnapshot()
{
    if (m_layoutHistoryApplying || !content_window)
        return;

    const QByteArray state = content_window->saveState(WindowStateVersion);
    if (state.isEmpty())
        return;
    if (!m_layoutUndoHistory.isEmpty() && m_layoutUndoHistory.last() == state) {
        updateLayoutHistoryActions();
        return;
    }

    m_layoutUndoHistory.append(state);
    while (m_layoutUndoHistory.size() > kMaxLayoutHistoryEntries)
        m_layoutUndoHistory.removeFirst();
    m_layoutRedoHistory.clear();
    updateLayoutHistoryActions();
}

void MainWindow::updateLayoutHistoryActions()
{
    if (m_undoLayoutAction)
        m_undoLayoutAction->setEnabled(m_layoutUndoHistory.size() > 1);
    if (m_redoLayoutAction)
        m_redoLayoutAction->setEnabled(!m_layoutRedoHistory.isEmpty());
}

void MainWindow::undoLayoutChange()
{
    if (!content_window || m_layoutUndoHistory.size() < 2)
        return;

    const QByteArray current = m_layoutUndoHistory.takeLast();
    const QByteArray target = m_layoutUndoHistory.last();

    m_layoutHistoryApplying = true;
    bool restored = false;
    for (int version = WindowStateVersion; version >= 1 && !restored; --version)
        restored = content_window->restoreState(target, version);
    m_layoutHistoryApplying = false;

    if (restored) {
        m_layoutRedoHistory.append(current);
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
    } else {
        m_layoutUndoHistory.append(current);
    }
    updateLayoutHistoryActions();
}

void MainWindow::redoLayoutChange()
{
    if (!content_window || m_layoutRedoHistory.isEmpty())
        return;

    const QByteArray target = m_layoutRedoHistory.takeLast();

    m_layoutHistoryApplying = true;
    bool restored = false;
    for (int version = WindowStateVersion; version >= 1 && !restored; --version)
        restored = content_window->restoreState(target, version);
    m_layoutHistoryApplying = false;

    if (restored) {
        m_layoutUndoHistory.append(target);
        while (m_layoutUndoHistory.size() > kMaxLayoutHistoryEntries)
            m_layoutUndoHistory.removeFirst();
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
    } else {
        m_layoutRedoHistory.append(target);
    }
    updateLayoutHistoryActions();
}

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
        if (!isActiveWindow() && emu_thread.isRunning() && !ui->actionPause->isChecked())
        {
            focus_auto_paused = true;
            emu_thread.setPaused(true);
        }
        else if (isActiveWindow() && focus_auto_paused)
        {
            focus_auto_paused = false;
            emu_thread.setPaused(false);
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
        auto remote = the_qml_bridge->getUSBDir() + QLatin1Char('/') + QFileInfo(local).fileName();
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
        if (m_debugDocks && m_debugDocks->console())
            m_debugDocks->console()->appendTaggedOutput(ConsoleTag::Uart, out);
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
        if (m_debugDocks) m_debugDocks->raise();
        if (m_debugDocks) {
            m_debugDocks->markDirty();
            m_debugDocks->refreshAll();
        }
        if (m_debugDocks && m_debugDocks->console())
            m_debugDocks->console()->focusInput();
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
        if (m_debugDocks) m_debugDocks->raise();
        if (m_debugDocks) {
            m_debugDocks->markDirty();
            m_debugDocks->refreshAll();
        }
        if (m_debugDocks && m_debugDocks->console())
            m_debugDocks->console()->focusInput();
    }
    else
    {
        debug_invalidate_cpu_snapshot();
        if (m_debugDocks) m_debugDocks->hideAutoShown();
    }
}

void MainWindow::debugStr(QString str)
{
    if (m_debugDocks && m_debugDocks->console()) {
        if (str.startsWith(QLatin1Char('>'))) {
            /* Command echo from debug line edit -- plain text, no tag */
            m_debugDocks->console()->appendOutput(str);
        } else {
            /* Debug engine output -- tagged and syntax-highlighted */
            m_debugDocks->console()->appendTaggedOutput(ConsoleTag::Debug, str);
        }

    }
}

void MainWindow::nlogStr(QString str)
{
    if (m_debugDocks && m_debugDocks->console())
        m_debugDocks->console()->appendTaggedOutput(ConsoleTag::Nlog, str);
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

void MainWindow::usblink_progress_callback(int progress, void *)
{
    MainWindow *mw = getMainWindow();
    if (!mw || !mw->ui)
        return;

    // TODO: Don't do a full refresh
    // Also refresh on error, in case of multiple transfers
    if ((progress == 100 || progress < 0) && usblink_queue_size() == 1)
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

    the_qml_bridge->setActive(mobile_ui);
    this->setActive(!mobile_ui);

    settings->setValue(QStringLiteral("lastUIMode"), mobile_ui ? 1 : 0);
}

void MainWindow::setActive(bool b)
{
    // There is no UniqueQueuedConnection, so we need to avoid duplicate connections
    // manually
    if (b == is_active)
        return;

    is_active = b;

    if (b)
    {
        connect(&emu_thread, SIGNAL(speedChanged(double)), this, SLOT(showSpeed(double)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(turboModeChanged(bool)), ui->buttonSpeed, SLOT(setChecked(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(usblinkChanged(bool)), this, SLOT(usblinkChanged(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(started(bool)), this, SLOT(started(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(paused(bool)), ui->actionPause, SLOT(setChecked(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(resumed(bool)), this, SLOT(resumed(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(suspended(bool)), this, SLOT(suspended(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(stopped()), this, SLOT(stopped()), Qt::QueuedConnection);

        // We might have missed a few events
        updateUIActionState(emu_thread.isRunning());
        ui->buttonSpeed->setChecked(turbo_mode);
        usblinkChanged(usblink_connected);
    }
    else
    {
        disconnect(&emu_thread, SIGNAL(speedChanged(double)), this, SLOT(showSpeed(double)));
        disconnect(&emu_thread, SIGNAL(turboModeChanged(bool)), ui->buttonSpeed, SLOT(setChecked(bool)));
        disconnect(&emu_thread, SIGNAL(usblinkChanged(bool)), this, SLOT(usblinkChanged(bool)));
        disconnect(&emu_thread, SIGNAL(started(bool)), this, SLOT(started(bool)));
        disconnect(&emu_thread, SIGNAL(paused(bool)), ui->actionPause, SLOT(setChecked(bool)));
        disconnect(&emu_thread, SIGNAL(resumed(bool)), this, SLOT(resumed(bool)));
        disconnect(&emu_thread, SIGNAL(suspended(bool)), this, SLOT(suspended(bool)));
        disconnect(&emu_thread, SIGNAL(stopped()), this, SLOT(stopped()));

        // Close the config dialog
        if (config_dialog)
            config_dialog->setProperty("visible", false);
    }

    setVisible(b);
}

void MainWindow::suspendToPath(QString path)
{
    emu_thread.suspend(path);
}

bool MainWindow::resumeFromPath(QString path)
{
    if (!emu_thread.resume(path))
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

void MainWindow::convertTabsToDocks()
{
    /* Legacy name kept for compatibility with existing call sites.
     * This function is the authoritative dock construction routine for
     * desktop UI mode and runs before restoreState(). */
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
    connect(editmode_toggle, SIGNAL(toggled(bool)), this, SLOT(setUIEditMode(bool)));

    docks_menu->addAction(editmode_toggle);

    QAction *resetLayoutAction = new QAction(tr("Reset Layout"), this);
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::resetDockLayout);
    docks_menu->addAction(resetLayoutAction);

    QMenu *layouts_menu = docks_menu->addMenu(tr("Layouts"));

    const auto saveLayoutProfileAction = [this](const QString &profileName) {
        const QJsonObject debugDockState = m_debugDocks ? m_debugDocks->serializeDockStates()
                                                        : QJsonObject();
        QString error;
        if (!saveLayoutProfile(content_window, profileName, WindowStateVersion,
                               debugDockState, &error)) {
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
        if (!restoreLayoutProfile(content_window, profileName, WindowStateVersion,
                                  &error, &debugDockState)) {
            QMessageBox::warning(this, tr("Load layout failed"),
                                 tr("Could not load layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        if (m_debugDocks && !debugDockState.isEmpty())
            m_debugDocks->restoreDockStates(debugDockState);
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
        showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
    };

    QAction *loadDefaultLayoutAction = layouts_menu->addAction(tr("Load Default"));
    QAction *loadDebugLayoutAction = layouts_menu->addAction(tr("Load Debugging"));
    QAction *loadCustomLayoutAction = layouts_menu->addAction(tr("Load Custom"));
    layouts_menu->addSeparator();
    QAction *saveDefaultLayoutAction = layouts_menu->addAction(tr("Save As Default"));
    QAction *saveDebugLayoutAction = layouts_menu->addAction(tr("Save As Debugging"));
    QAction *saveCustomLayoutAction = layouts_menu->addAction(tr("Save As Custom"));
    layouts_menu->addSeparator();
    QAction *openLayoutFolderAction = layouts_menu->addAction(tr("Open Layout Folder"));

    connect(loadDefaultLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("default")); });
    connect(loadDebugLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("debugging")); });
    connect(loadCustomLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("custom")); });
    connect(saveDefaultLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("default")); });
    connect(saveDebugLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("debugging")); });
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

    QMenu *focusMenu = docks_menu->addMenu(tr("Dock Focus Policy"));
    QActionGroup *focusGroup = new QActionGroup(focusMenu);
    focusGroup->setExclusive(true);

    QAction *focusAlwaysAction = focusMenu->addAction(tr("Always Raise"));
    focusAlwaysAction->setCheckable(true);
    focusAlwaysAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::Always));
    focusGroup->addAction(focusAlwaysAction);

    QAction *focusExplicitAction = focusMenu->addAction(tr("Raise on Explicit Actions"));
    focusExplicitAction->setCheckable(true);
    focusExplicitAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::ExplicitOnly));
    focusGroup->addAction(focusExplicitAction);

    QAction *focusNeverAction = focusMenu->addAction(tr("Never Raise Automatically"));
    focusNeverAction->setCheckable(true);
    focusNeverAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::Never));
    focusGroup->addAction(focusNeverAction);

    auto applyDockFocusPolicy = [this](int value) {
        DebugDockManager::DockFocusPolicy policy = DebugDockManager::DockFocusPolicy::Always;
        if (value == static_cast<int>(DebugDockManager::DockFocusPolicy::ExplicitOnly))
            policy = DebugDockManager::DockFocusPolicy::ExplicitOnly;
        else if (value == static_cast<int>(DebugDockManager::DockFocusPolicy::Never))
            policy = DebugDockManager::DockFocusPolicy::Never;
        settings->setValue(QString::fromLatin1(kSettingDockFocusPolicy), static_cast<int>(policy));
        if (m_debugDocks)
            m_debugDocks->setDockFocusPolicy(policy);
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
                                        true);
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

        content_window->addDockWidget(Qt::RightDockWidgetArea, dw);
    }

    /* Keep pointers for layout reset/re-link behavior. */
    m_dock_files = dock_files;
    m_dock_keypad = dock_keypad;

    /* STEP 3: Create utility docks that were not tab pages. */
    /* Create NAND Browser dock */
    m_nandBrowser = new NandBrowserWidget(content_window);
    m_dock_nand = createMainDock(tr("NAND Browser"),
                                 m_nandBrowser,
                                 QString::fromLatin1(mainDockObjectName(MainDockId::NandBrowser)),
                                 Qt::RightDockWidgetArea,
                                 docks_menu);

    /* Create Hardware Configuration dock */
    m_hwConfig = new HwConfigWidget(content_window);
    m_dock_hwconfig = createMainDock(tr("Hardware Config"),
                                     m_hwConfig,
                                     QString::fromLatin1(mainDockObjectName(MainDockId::HwConfig)),
                                     Qt::RightDockWidgetArea,
                                     docks_menu);

    /* External LCD as an optional floating dock (instead of a separate window). */
    m_dock_ext_lcd = createMainDock(tr("Screen (External)"),
                                    &lcd,
                                    QString::fromLatin1(mainDockObjectName(MainDockId::ExternalLCD)),
                                    Qt::RightDockWidgetArea,
                                    docks_menu,
                                    QIcon(),
                                    false);
    m_dock_ext_lcd->setFloating(true);
    m_dock_ext_lcd->hide();
    connect(m_dock_ext_lcd, &QDockWidget::visibilityChanged, this, [this](bool visible) {
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
        connect(m_dock_keypad, &QDockWidget::topLevelChanged, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                auto src = ui->keypadWidget->source();
                ui->keypadWidget->setSource(QUrl());
                ui->keypadWidget->setSource(src);
            });
        });
    }

    // Keep default corner behavior so all docks behave like regular Qt docks.

    /* STEP 5: Create debugger docks and finalize initial dock visibility. */
    /* Create the CEmu-style debugger docks via DebugDockManager */
    m_debugDocks = std::make_unique<DebugDockManager>(content_window, material_icon_font, this);
    m_debugDocks->createDocks(docks_menu);
    connect(m_debugDocks.get(), &DebugDockManager::debugCommand,
            this, &MainWindow::debuggerCommand);

    int savedFocusPolicy = settings->value(QString::fromLatin1(kSettingDockFocusPolicy),
                                           static_cast<int>(DebugDockManager::DockFocusPolicy::Always)).toInt();
    if (savedFocusPolicy < static_cast<int>(DebugDockManager::DockFocusPolicy::Always) ||
        savedFocusPolicy > static_cast<int>(DebugDockManager::DockFocusPolicy::Never)) {
        savedFocusPolicy = static_cast<int>(DebugDockManager::DockFocusPolicy::Always);
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
        connect(dock, &QDockWidget::dockLocationChanged, this,
                [this](Qt::DockWidgetArea) { scheduleLayoutHistoryCapture(); });
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this](bool) { scheduleLayoutHistoryCapture(); });
        connect(dock, &QDockWidget::visibilityChanged, this,
                [this](bool) { scheduleLayoutHistoryCapture(); });
    }

    ui->tabWidget->setHidden(true);
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
    if (m_debugDocks) m_debugDocks->retranslate();
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
    if (!the_qml_bridge || !the_qml_bridge->getGDBEnabled())
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
    const int port = the_qml_bridge->getGDBPort();

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
    if (usblink_connected) {
        hw_override_set_usb_cable_connected(0);
        usblink_queue_reset();
        usblink_reset();
    } else {
        hw_override_set_usb_cable_connected(1);
        usblink_connect();
    }

    usblinkChanged(false);
}

void MainWindow::usblinkChanged(bool state)
{
    ui->actionConnect->setText(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->actionConnect->setChecked(state);
    ui->buttonUSB->setToolTip(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->buttonUSB->setChecked(state);
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
    if (the_qml_bridge->getCurrentKitId() == -1)
        the_qml_bridge->useDefaultKit();

    applyQMLBridgeSettings();

    auto snapshot_path = the_qml_bridge->getSnapshotPath();
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
    auto snapshot_path = the_qml_bridge->getSnapshotPath();
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
    QString snapshot_path = the_qml_bridge ? the_qml_bridge->getSnapshotPath() : QString();
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

void MainWindow::resetDockLayout()
{
    /* Reset LCD and Controls docks to RightDockWidgetArea (main area) */
    if (m_dock_lcd) {
        m_dock_lcd->setFloating(false);
        content_window->addDockWidget(Qt::RightDockWidgetArea, m_dock_lcd);
        m_dock_lcd->setVisible(true);
    }
    if (m_dock_controls) {
        m_dock_controls->setFloating(false);
        content_window->addDockWidget(Qt::RightDockWidgetArea, m_dock_controls);
        m_dock_controls->setVisible(true);
    }

    /* Reset file/keypad/NAND/HW config docks as regular docks. */
    for (QDockWidget *dw : {static_cast<QDockWidget*>(m_dock_files),
                            static_cast<QDockWidget*>(m_dock_keypad),
                            static_cast<QDockWidget*>(m_dock_nand),
                            static_cast<QDockWidget*>(m_dock_hwconfig)}) {
        if (dw) {
            dw->setFloating(false);
            content_window->addDockWidget(Qt::RightDockWidgetArea, dw);
            dw->setVisible(true);
        }
    }

    if (m_debugDocks) m_debugDocks->resetLayout();
    scheduleLayoutHistoryCapture();
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

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (config_dialog)
        config_dialog->setProperty("visible", false);

    if (flash_dialog)
        flash_dialog->setProperty("visible", false);

    if (!close_after_suspend &&
        settings->value(QStringLiteral("suspendOnClose")).toBool() && emu_thread.isRunning() && exiting == false)
    {
        close_after_suspend = true;
        qDebug("Suspending...");
        suspend();
        e->ignore();
        return;
    }

    if (emu_thread.isRunning() && !emu_thread.stop())
        qDebug("Terminating emulator thread failed.");

    // Persist layout/geometry while the full dock tree is still alive.
    savePersistentUiState();

    QMainWindow::closeEvent(e);
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
    if (the_qml_bridge->getKitModel()->rowCount() != ui->menuRestart_with_Kit->actions().size())
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

    auto &&kit_model = the_qml_bridge->getKitModel();
    for (auto &&kit : kit_model->getKits())
    {
        QAction *action = ui->menuRestart_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, SIGNAL(triggered()), this, SLOT(startKit()));

        action = ui->menuBoot_Diags_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, SIGNAL(triggered()), this, SLOT(startKitDiags()));
    }
}

void MainWindow::updateWindowTitle()
{
    // Need to update window title if kit is active
    int kitIndex = the_qml_bridge->kitIndexForID(the_qml_bridge->getCurrentKitId());
    if (kitIndex >= 0)
    {
        auto name = the_qml_bridge->getKitModel()->getKits()[kitIndex].name;
        setWindowTitle(tr("Firebird Emu - %1").arg(name));
    }
    else
        setWindowTitle(tr("Firebird Emu"));
}

void MainWindow::applyQMLBridgeSettings()
{
    // Reload the current kit
    the_qml_bridge->useKit(the_qml_bridge->getCurrentKitId());

    emu_thread.port_gdb = the_qml_bridge->getGDBEnabled() ? the_qml_bridge->getGDBPort() : 0;
    emu_thread.port_rdbg = the_qml_bridge->getRDBEnabled() ? the_qml_bridge->getRDBPort() : 0;
}

void MainWindow::restart()
{
    /* If there's no kit set, use the default kit */
    if (the_qml_bridge->getCurrentKitId() == -1)
        the_qml_bridge->useDefaultKit();

    applyQMLBridgeSettings();

    if (emu_thread.boot1.isEmpty())
    {
        QMessageBox::critical(this, tr("No boot1 set"), tr("Before you can start the emulation, you have to select a proper boot1 file."));
        return;
    }

    if (emu_thread.flash.isEmpty())
    {
        QMessageBox::critical(this, tr("No flash image loaded"), tr("Before you can start the emulation, you have to load a proper flash file.\n"
                                                                    "You can create one via Flash->Create Flash in the menu."));
        return;
    }

    if (emu_thread.stop())
        emu_thread.start();
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
    the_qml_bridge->setCurrentKit(kit_id);
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
    the_qml_bridge->setCurrentKit(kit_id);
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

bool QQuickWidgetLessBroken::event(QEvent *event)
{
    if (event->type() == QEvent::Leave)
    {
        QMouseEvent ev(QEvent::MouseMove, QPointF(0, 0), QPointF(0, 0), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QQuickWidget::event(&ev);
    }

    return QQuickWidget::event(event);
}
