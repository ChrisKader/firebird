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
#include <QVBoxLayout>
#include <QBoxLayout>
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
#include <QDateTime>
#include <QAbstractButton>

#include <array>
#include <climits>
#include <utility>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/DockWidget.h>
    #include <kddockwidgets/KDDockWidgets.h>
    #include <kddockwidgets/LayoutSaver.h>
    #include <kddockwidgets/qtcommon/View.h>
    #include <kddockwidgets/core/DockWidget.h>
    #include <kddockwidgets/core/TitleBar.h>
    #include <kddockwidgets/core/View.h>
#endif

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
#include "app/powercontrol.h"
#include "app/baselinelayout.h"
#include "ui/framebuffer.h"
#include "ui/keypadbridge.h"
#include "debugger/dockmanager.h"

#include "debugger/disassembly/disassemblywidget.h"
#include "debugger/hexview/hexviewwidget.h"
#include "debugger/console/consolewidget.h"
#include "debugger/nandbrowser/nandbrowserwidget.h"
#include "debugger/hwconfig/hwconfigwidget.h"

#ifndef FIREBIRD_USE_KDDOCKWIDGETS
// Legacy saveState version kept only for non-KDD fallback builds.
static const constexpr int WindowStateVersion = 9;
#endif

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
static constexpr const char *kSettingHwBatteryPresentOverride = "hwBatteryPresentOverride";
static constexpr const char *kSettingHwUsbCableConnectedOverride = "hwUsbCableConnectedOverride";
static constexpr const char *kSettingHwUsbOtgOverride = "hwUsbOtgOverride";
static constexpr const char *kSettingHwDockAttachedOverride = "hwDockAttachedOverride";
static constexpr const char *kSettingHwVbusMvOverride = "hwVbusMvOverride";
static constexpr const char *kSettingHwVsledMvOverride = "hwVsledMvOverride";
static constexpr const char *kSettingWindowLayoutJson = "windowLayoutJson";
static constexpr const char *kSettingDockLayoutJson = "dockLayoutJson";
static constexpr const char *kSettingLayoutProfile = "layoutProfile";
static constexpr const char *kSettingDebugDockStateJson = "debugDockStateJson";
static constexpr const char *kSettingDockFocusPolicy = "dockFocusPolicy";
static constexpr const char *kLayoutSchemaKDDV1 = "firebird.kdd.layout.v1";
static constexpr const char *kLayoutSchemaLegacyQMainWindowV1 = "firebird.qmainwindow.layout.v1";
static constexpr int kMaxLayoutHistoryEntries = 10;

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

struct HwOverrides {
    int batteryRaw = -1;
    int charging = -1;
    int brightness = -1;
    int keypadType = -1;
    int batteryMv = -1;
    int chargerState = -1;
    int batteryPresent = -1;
    int usbCableConnected = -1;
    int usbOtgCable = -1;
    int dockAttached = -1;
    int vbusMv = -1;
    int vsledMv = -1;
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
    overrides.batteryPresent = readValue(kSettingHwBatteryPresentOverride);
    overrides.usbCableConnected = readValue(kSettingHwUsbCableConnectedOverride);
    overrides.usbOtgCable = readValue(kSettingHwUsbOtgOverride);
    overrides.dockAttached = readValue(kSettingHwDockAttachedOverride);
    overrides.vbusMv = readValue(kSettingHwVbusMvOverride);
    overrides.vsledMv = readValue(kSettingHwVsledMvOverride);
    return overrides;
}

static void writeHwOverridesToSettings(QSettings *settings, const HwOverrides &overrides)
{
    if (!settings)
        return;
    const std::array<std::pair<const char *, int>, 12> values = {{
        { kSettingHwBatteryOverride, overrides.batteryRaw },
        { kSettingHwChargingOverride, overrides.charging },
        { kSettingHwBrightnessOverride, overrides.brightness },
        { kSettingHwKeypadTypeOverride, overrides.keypadType },
        { kSettingHwBatteryMvOverride, overrides.batteryMv },
        { kSettingHwChargerStateOverride, overrides.chargerState },
        { kSettingHwBatteryPresentOverride, overrides.batteryPresent },
        { kSettingHwUsbCableConnectedOverride, overrides.usbCableConnected },
        { kSettingHwUsbOtgOverride, overrides.usbOtgCable },
        { kSettingHwDockAttachedOverride, overrides.dockAttached },
        { kSettingHwVbusMvOverride, overrides.vbusMv },
        { kSettingHwVsledMvOverride, overrides.vsledMv },
    }};
    for (const auto &entry : values)
        settings->setValue(QString::fromLatin1(entry.first), entry.second);
}

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

#ifndef FIREBIRD_USE_KDDOCKWIDGETS
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
#endif

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
static KDDockWidgets::QtWidgets::MainWindow *asKDDMainWindow(QMainWindow *window)
{
    return dynamic_cast<KDDockWidgets::QtWidgets::MainWindow *>(window);
}

static KDDockWidgets::Location toKDDLocation(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return KDDockWidgets::Location_OnLeft;
    case Qt::TopDockWidgetArea: return KDDockWidgets::Location_OnTop;
    case Qt::RightDockWidgetArea: return KDDockWidgets::Location_OnRight;
    case Qt::BottomDockWidgetArea: return KDDockWidgets::Location_OnBottom;
    default: return KDDockWidgets::Location_OnRight;
    }
}
#endif

static void addDockWidgetCompat(QMainWindow *window,
                                DockWidget *dock,
                                Qt::DockWidgetArea area,
                                DockWidget *relativeTo = nullptr,
                                bool startHidden = false,
                                bool preserveCurrentSize = false,
                                const QSize &preferredSize = QSize())
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (auto *kdd = asKDDMainWindow(window)) {
        KDDockWidgets::InitialOption initial;
        if (preferredSize.isValid() && preferredSize.width() > 0 && preferredSize.height() > 0) {
            initial.preferredSize = preferredSize;
        }
        if (preserveCurrentSize) {
            const QSize current = dock->size();
            if (current.isValid() && current.width() > 0 && current.height() > 0)
                initial.preferredSize = current;
        }
        if (!initial.preferredSize.isValid() && dock->widget()) {
            const QSize hinted = dock->widget()->sizeHint();
            if (hinted.isValid() && hinted.width() > 0 && hinted.height() > 0)
                initial.preferredSize = hinted;
        }
        if (startHidden)
            initial.visibility = KDDockWidgets::InitialVisibilityOption::StartHidden;
        kdd->addDockWidget(dock, toKDDLocation(area), relativeTo, initial);
        return;
    }
#else
    Q_UNUSED(relativeTo);
    Q_UNUSED(startHidden);
    Q_UNUSED(preferredSize);
    window->addDockWidget(area, dock);
#endif
}

static Qt::DockWidgetArea dockAreaFromString(const QString &name)
{
    if (name == QLatin1String("left"))
        return Qt::LeftDockWidgetArea;
    if (name == QLatin1String("right"))
        return Qt::RightDockWidgetArea;
    if (name == QLatin1String("top"))
        return Qt::TopDockWidgetArea;
    if (name == QLatin1String("bottom"))
        return Qt::BottomDockWidgetArea;
    return Qt::RightDockWidgetArea;
}

static QByteArray serializeDockLayout(QMainWindow *window)
{
    if (!window)
        return QByteArray();
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (asKDDMainWindow(window)) {
        KDDockWidgets::LayoutSaver saver;
        return saver.serializeLayout();
    }
#else
    return window->saveState(WindowStateVersion);
#endif
    return QByteArray();
}

static bool restoreDockLayout(QMainWindow *window,
                              const QByteArray &layoutData,
                              QString *errorOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }
    if (layoutData.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("layout data is empty");
        return false;
    }
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (asKDDMainWindow(window)) {
        KDDockWidgets::LayoutSaver relativeSaver(KDDockWidgets::RestoreOption_RelativeToMainWindow);
        if (relativeSaver.restoreLayout(layoutData))
            return true;

        KDDockWidgets::LayoutSaver saver;
        if (saver.restoreLayout(layoutData))
            return true;
        if (errorOut)
            *errorOut = QStringLiteral("LayoutSaver::restoreLayout failed (relative and absolute)");
        return false;
    }
#else
    for (int version = WindowStateVersion; version >= 1; --version) {
        if (window->restoreState(layoutData, version))
            return true;
    }
    if (errorOut)
        *errorOut = QStringLiteral("restoreState failed for all supported versions");
    return false;
#endif
    if (errorOut)
        *errorOut = QStringLiteral("unsupported window type for dock layout restore");
    return false;
}

static QJsonObject makeDockLayoutJson(QMainWindow *window)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kLayoutSchemaKDDV1));
    root.insert(QStringLiteral("layoutBase64"),
                QString::fromLatin1(serializeDockLayout(window).toBase64()));

    QJsonArray docks;
    if (window) {
        const auto dockChildren = window->findChildren<DockWidget *>();
        for (DockWidget *dw : dockChildren) {
            if (!dw)
                continue;
            QJsonObject dock;
            dock.insert(QStringLiteral("objectName"), dw->objectName());
            dock.insert(QStringLiteral("title"), dw->windowTitle());
            dock.insert(QStringLiteral("visible"), dw->isVisible());
            dock.insert(QStringLiteral("floating"), dw->isFloating());
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
            dock.insert(QStringLiteral("area"), QStringLiteral("none"));
#else
            dock.insert(QStringLiteral("area"), dockAreaToString(window->dockWidgetArea(dw)));
#endif
            dock.insert(QStringLiteral("geometryBase64"),
                        QString::fromLatin1(dw->saveGeometry().toBase64()));
            docks.append(dock);
        }
    }
    root.insert(QStringLiteral("docks"), docks);
    return root;
}

static bool extractLayoutDataFromObject(const QJsonObject &root,
                                        QByteArray *layoutOut,
                                        QString *errorOut = nullptr)
{
    const QString layoutBase64 = root.value(QStringLiteral("layoutBase64")).toString();
    if (layoutBase64.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("layoutBase64 missing");
        return false;
    }

    const QByteArray layoutData = QByteArray::fromBase64(layoutBase64.toLatin1());
    if (layoutData.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("layoutBase64 decode failed");
        return false;
    }

    if (layoutOut)
        *layoutOut = layoutData;
    return true;
}

static bool restoreLegacyDockHints(QMainWindow *window,
                                   const QJsonObject &root,
                                   QString *errorOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }

    const QJsonArray docks = root.value(QStringLiteral("docks")).toArray();
    bool restoredAny = false;
    for (const QJsonValue &value : docks) {
        if (!value.isObject())
            continue;
        const QJsonObject dockState = value.toObject();
        const QString objectName = dockState.value(QStringLiteral("objectName")).toString();
        if (objectName.isEmpty())
            continue;

        DockWidget *dock = window->findChild<DockWidget *>(objectName);
        if (!dock)
            continue;

        const bool floating = dockState.value(QStringLiteral("floating")).toBool(false);
        if (floating) {
            dock->setFloating(true);
        } else {
            dock->setFloating(false);
            const Qt::DockWidgetArea area =
                dockAreaFromString(dockState.value(QStringLiteral("area")).toString());
            addDockWidgetCompat(window, dock, area);
        }

        const QByteArray geometry = QByteArray::fromBase64(
            dockState.value(QStringLiteral("geometryBase64")).toString().toLatin1());
        if (!geometry.isEmpty())
            dock->restoreGeometry(geometry);

        if (dockState.contains(QStringLiteral("visible")))
            dock->setVisible(dockState.value(QStringLiteral("visible")).toBool(true));

        const QString title = dockState.value(QStringLiteral("title")).toString();
        if (!title.isEmpty())
            dock->setWindowTitle(title);

        restoredAny = true;
    }

    if (restoredAny)
        return true;

    if (errorOut)
        *errorOut = QStringLiteral("legacy layout did not match any current docks");
    return false;
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

static QString backupCorruptLayoutProfile(const QString &filePath)
{
    QFileInfo info(filePath);
    if (!info.exists())
        return QString();

    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QString backupPath = filePath + QStringLiteral(".corrupt.") + stamp + QStringLiteral(".json");
    QFile::remove(backupPath);
    if (!QFile::copy(filePath, backupPath))
        return QString();
    return backupPath;
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
                              const QJsonObject &debugDockState = QJsonObject(),
                              const QJsonObject &coreDockConnections = QJsonObject(),
                              QString *errorOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }
    if (!ensureLayoutProfilesDir(errorOut))
        return false;

    QJsonObject layoutJson = makeDockLayoutJson(window);
    if (!debugDockState.isEmpty())
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
    if (!coreDockConnections.isEmpty())
        layoutJson.insert(QStringLiteral("coreDockConnections"), coreDockConnections);
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

static bool restoreLayoutProfile(QMainWindow *window, const QString &profileName,
                                 QString *errorOut = nullptr,
                                 QJsonObject *debugDockStateOut = nullptr,
                                 QJsonObject *coreDockConnectionsOut = nullptr)
{
    if (!window) {
        if (errorOut)
            *errorOut = QStringLiteral("window is null");
        return false;
    }

    QString dirError;
    if (!ensureLayoutProfilesDir(&dirError)) {
        if (errorOut)
            *errorOut = dirError;
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
        const QString backupPath = backupCorruptLayoutProfile(filePath);
        if (errorOut)
            *errorOut = backupPath.isEmpty()
                    ? QStringLiteral("invalid JSON in %1").arg(filePath)
                    : QStringLiteral("invalid JSON in %1 (backup: %2)").arg(filePath, backupPath);
        return false;
    }

    const QJsonObject root = doc.object();
    if (debugDockStateOut)
        *debugDockStateOut = root.value(QStringLiteral("debugDockState")).toObject();
    if (coreDockConnectionsOut)
        *coreDockConnectionsOut = root.value(QStringLiteral("coreDockConnections")).toObject();

    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (schema == QLatin1String(kLayoutSchemaKDDV1) || root.contains(QStringLiteral("layoutBase64"))) {
        QByteArray layoutData;
        QString parseError;
        if (!extractLayoutDataFromObject(root, &layoutData, &parseError)) {
            const QString backupPath = backupCorruptLayoutProfile(filePath);
            if (errorOut)
                *errorOut = backupPath.isEmpty()
                        ? QStringLiteral("%1 in %2").arg(parseError, filePath)
                        : QStringLiteral("%1 in %2 (backup: %3)").arg(parseError, filePath, backupPath);
            return false;
        }
        QString restoreError;
        if (restoreDockLayout(window, layoutData, &restoreError))
            return true;
        if (errorOut)
            *errorOut = QStringLiteral("%1 in %2").arg(restoreError, filePath);
        return false;
    }

    if (schema == QLatin1String(kLayoutSchemaLegacyQMainWindowV1) ||
        root.contains(QStringLiteral("windowStateBase64")) ||
        root.contains(QStringLiteral("docks"))) {
        if (restoreLegacyDockHints(window, root, errorOut))
            return true;
        if (errorOut && errorOut->isEmpty())
            *errorOut = QStringLiteral("legacy layout restore failed for %1").arg(filePath);
        return false;
    }

    if (errorOut)
        *errorOut = QStringLiteral("unsupported layout schema in %1: %2").arg(filePath, schema);
    return false;
}

/* WidgetTheme, applyPaletteColors, setWidgetBackground now in widgettheme.h/cpp */

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
    if (auto *kdd = asKDDMainWindow(content_window))
        kdd->setPersistentCentralWidget(placeholder);
#else
    content_window->setCentralWidget(placeholder);
#endif
    ui->mainLayout->addWidget(content_window);

    // Extract LCDWidget from ui->frame into its own dock
    {
        m_dock_lcd = createMainDock(tr("Screen"),
                                    ui->lcdView,
                                    QString::fromLatin1(mainDockObjectName(MainDockId::LCD)),
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
                                         QString::fromLatin1(mainDockObjectName(MainDockId::Controls)),
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

    /* Dock/window initialization order is significant:
     * 1) create all main/debug docks (including dynamic extra hex docks),
     * 2) restore geometry/dock layout against those concrete dock objects,
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

    // Restore dock layout from named profiles; if unavailable, use built-in C++ default baseline.
    bool restored = false;
    bool usedBuiltInDefaultBaseline = false;
    QString appliedStartupProfile;
    const QString startupProfile = settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed();
    QJsonObject restoredDebugDockState;
    QJsonObject restoredCoreDockConnections;
    const QString autoProfile = startupProfile.isEmpty() ? QStringLiteral("default") : startupProfile;
    if (!autoProfile.isEmpty()) {
        QString profileError;
        if (restoreLayoutProfile(content_window,
                                 autoProfile,
                                 &profileError,
                                 &restoredDebugDockState,
                                 &restoredCoreDockConnections)) {
            restored = true;
        } else {
            qDebug("profile restore failed (%s): %s",
                   autoProfile.toUtf8().constData(),
                   profileError.toUtf8().constData());
        }
    }
    if (!restored) {
        qDebug("profile unavailable or invalid -- applying built-in default layout");
        resetDockLayout();
        usedBuiltInDefaultBaseline = true;
        restored = true;
        appliedStartupProfile = QStringLiteral("default");
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), appliedStartupProfile);
    } else {
        appliedStartupProfile = autoProfile;
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), appliedStartupProfile);
    }

    if (m_debugDocks) {
        if (!restoredDebugDockState.isEmpty()) {
            m_debugDocks->restoreDockStates(restoredDebugDockState);
        } else if (!usedBuiltInDefaultBaseline) {
            QJsonParseError debugStateErr = {};
            const QString savedDebugState = settings->value(QString::fromLatin1(kSettingDebugDockStateJson)).toString();
            const QJsonDocument debugStateDoc = QJsonDocument::fromJson(savedDebugState.toUtf8(), &debugStateErr);
            if (debugStateErr.error == QJsonParseError::NoError && debugStateDoc.isObject())
                m_debugDocks->restoreDockStates(debugStateDoc.object());
        }
    }

    restoreCoreDockConnections(restoredCoreDockConnections);

    m_layoutUndoHistory.clear();
    m_layoutRedoHistory.clear();
    captureLayoutHistorySnapshot();

    // KDD layout restore can run before final dock geometry settles at startup.
    // Re-apply the selected profile once on the next event loop tick so relaunch
    // matches explicit "Load layout profile" behavior.
    if (!appliedStartupProfile.isEmpty()) {
        QTimer::singleShot(0, this, [this, appliedStartupProfile]() {
            QString profileError;
            QJsonObject debugDockState;
            QJsonObject coreDockConnections;
            if (restoreLayoutProfile(content_window,
                                     appliedStartupProfile,
                                     &profileError,
                                     &debugDockState,
                                     &coreDockConnections)) {
                if (m_debugDocks && !debugDockState.isEmpty())
                    m_debugDocks->restoreDockStates(debugDockState);
                restoreCoreDockConnections(coreDockConnections);
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                captureLayoutHistorySnapshot();
                return;
            }

            if (appliedStartupProfile == QLatin1String("default")) {
                resetDockLayout();
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                captureLayoutHistorySnapshot();
                return;
            }

            qDebug("deferred profile restore failed (%s): %s",
                   appliedStartupProfile.toUtf8().constData(),
                   profileError.toUtf8().constData());
        });
    }

    m_lcdKeypadLinked = settings->value(QStringLiteral("lcdKeypadLinked"), false).toBool();

    // Restore HW config overrides.
    // Battery/charger overrides are coupled: never restore a forced charger state
    // unless battery override itself is active.
    const HwOverrides hw = readHwOverridesFromSettings(settings);
    auto clampTriState = [](int value) -> int8_t {
        if (value < 0)
            return -1;
        return value ? 1 : 0;
    };
    const bool forceCx2SafeBaseline = emulate_cx2 || likelyCx2StartupKit(qmlBridge());
    const int8_t batteryPresent = forceCx2SafeBaseline ? 1 : clampTriState(hw.batteryPresent);
    const int8_t usbCable = forceCx2SafeBaseline ? 0 : clampTriState(hw.usbCableConnected);
    const int8_t usbOtg = forceCx2SafeBaseline ? 0 : clampTriState(hw.usbOtgCable);
    const int8_t dockAttached = forceCx2SafeBaseline ? 0 : clampTriState(hw.dockAttached);

    hw_override_set_battery_present(batteryPresent);
    hw_override_set_usb_cable_connected(usbCable);
    hw_override_set_usb_otg_cable(usbOtg);
    hw_override_set_dock_attached(dockAttached);

    int restoredBatteryMv = forceCx2SafeBaseline ? -1 : hw.batteryMv;
    /* Old settings may contain non-mV payloads (for example bool/int flags)
     * in the battery-mV key. Reject out-of-range values so CX II falls back
     * to the model default instead of clamping to a fake 3000mV low battery. */
    if (restoredBatteryMv >= 0
            && (restoredBatteryMv < 3000 || restoredBatteryMv > 4200))
        restoredBatteryMv = -1;

    int restoredVbusMv = forceCx2SafeBaseline ? 0 : hw.vbusMv;
    int restoredVsledMv = forceCx2SafeBaseline ? 0 : hw.vsledMv;
    if (restoredVbusMv > 5500)
        restoredVbusMv = 5500;
    if (restoredVsledMv > 5500)
        restoredVsledMv = 5500;
    /* Normalize persisted rail overrides so "disconnected" truly means no
     * external power at boot. This avoids stale non-zero rail values causing
     * charging state without any cable/dock attachment. */
    if (usbOtg > 0 || usbCable <= 0)
        restoredVbusMv = 0;
    if (dockAttached <= 0)
        restoredVsledMv = 0;
    else if (restoredVsledMv < 0)
        restoredVsledMv = 0;
    hw_override_set_vbus_mv(restoredVbusMv);
    hw_override_set_vsled_mv(restoredVsledMv);
    const bool batteryOverrideActive = (restoredBatteryMv >= 0);
    if (batteryOverrideActive) {
        /* Legacy raw battery override is ignored for CX II power model. */
        hw_override_set_adc_battery_level(-1);
        hw_override_set_battery_mv(restoredBatteryMv);
        /* Never restore legacy forced charging state on CX II.
         * Charging status should come from physical rails/events only. */
        hw_override_set_adc_charging(-1);
        hw_override_set_charger_state(CHARGER_AUTO);
    } else {
        hw_override_set_adc_battery_level(-1);
        hw_override_set_battery_mv(-1);
        hw_override_set_adc_charging(-1);
        hw_override_set_charger_state(CHARGER_AUTO);
    }
    hw_override_set_lcd_contrast(static_cast<int16_t>(hw.brightness));
    hw_override_set_adc_keypad_type(static_cast<int16_t>(hw.keypadType));
    PowerControl::refreshPowerState();
    if (m_hwConfig)
        m_hwConfig->syncOverridesFromGlobals();

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
    if (!settings || m_persistentUiStateSaved)
        return;

    // Save external LCD dock geometry
    settings->setValue(QStringLiteral("extLCDGeometry"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->saveGeometry() : QByteArray());
    settings->setValue(QStringLiteral("extLCDVisible"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->isVisible() : false);

    // Save dock layout and geometry
    const QByteArray layoutData = serializeDockLayout(content_window);
    qDebug("Saving dock layout: %lld bytes", (long long)layoutData.size());
    settings->setValue(QString::fromLatin1(kSettingDockLayoutJson),
                       QString::fromLatin1(layoutData.toBase64()));
    QJsonObject layoutJson = makeDockLayoutJson(content_window);
    QJsonObject debugDockState;
    const QJsonObject coreDockConnections = serializeCoreDockConnections();
    if (m_debugDocks) {
        debugDockState = m_debugDocks->serializeDockStates();
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
        settings->setValue(QString::fromLatin1(kSettingDebugDockStateJson),
                           QString::fromUtf8(QJsonDocument(debugDockState).toJson(QJsonDocument::Compact)));
    }
    layoutJson.insert(QStringLiteral("coreDockConnections"), coreDockConnections);
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
    if (!saveLayoutProfile(content_window,
                           activeProfile,
                           debugDockState,
                           coreDockConnections,
                           &profileError)) {
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
        static_cast<int>(hw_override_get_battery_present()),
        static_cast<int>(hw_override_get_usb_cable_connected()),
        static_cast<int>(hw_override_get_usb_otg_cable()),
        static_cast<int>(hw_override_get_dock_attached()),
        hw_override_get_vbus_mv(),
        hw_override_get_vsled_mv(),
    };
    writeHwOverridesToSettings(settings, hw);

    settings->sync();
    m_persistentUiStateSaved = true;
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

    const QByteArray state = serializeDockLayout(content_window);
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
    const bool restored = restoreDockLayout(content_window, target);
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
    const bool restored = restoreDockLayout(content_window, target);
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
        const QJsonObject debugDockState = m_debugDocks ? m_debugDocks->serializeDockStates()
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
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
                return;
            }
            QMessageBox::warning(this, tr("Load layout failed"),
                                 tr("Could not load layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }

        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        if (m_debugDocks && !debugDockState.isEmpty())
            m_debugDocks->restoreDockStates(debugDockState);
        restoreCoreDockConnections(coreDockConnections);
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
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
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
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

    /* STEP 3: Create utility docks that were not tab pages. */
    /* Create NAND Browser dock */
    m_nandBrowser = new NandBrowserWidget(content_window);
    m_dock_nand = createMainDock(tr("NAND Browser"),
                                 m_nandBrowser,
                                 QString::fromLatin1(mainDockObjectName(MainDockId::NandBrowser)),
                                 Qt::RightDockWidgetArea,
                                 docks_menu,
                                 QIcon(),
                                 true,
                                 true,
                                 false);

    /* Create Hardware Configuration dock */
    m_hwConfig = new HwConfigWidget(content_window);
    m_dock_hwconfig = createMainDock(tr("Hardware Config"),
                                     m_hwConfig,
                                     QString::fromLatin1(mainDockObjectName(MainDockId::HwConfig)),
                                     Qt::RightDockWidgetArea,
                                     docks_menu,
                                     QIcon(),
                                     true,
                                     true,
                                     false);

    /* External LCD as an optional floating dock (instead of a separate window). */
    m_dock_ext_lcd = createMainDock(tr("Screen (External)"),
                                    &lcd,
                                    QString::fromLatin1(mainDockObjectName(MainDockId::ExternalLCD)),
                                    Qt::RightDockWidgetArea,
                                    docks_menu,
                                    QIcon(),
                                    false,
                                    true,
                                    false);
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
