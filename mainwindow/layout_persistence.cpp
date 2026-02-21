#include "mainwindow/layout_persistence.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QStandardPaths>

#include "ui/dockbackend.h"
#include "ui/dockwidget.h"

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/LayoutSaver.h>
    #include <kddockwidgets/MainWindow.h>
#endif

#ifndef FIREBIRD_USE_KDDOCKWIDGETS
// Legacy saveState version kept only for non-KDD fallback builds.
static constexpr int WindowStateVersion = 9;
#endif

static constexpr const char *kLayoutSchemaKDDV1 = "firebird.kdd.layout.v1";
static constexpr const char *kLayoutSchemaLegacyQMainWindowV1 = "firebird.qmainwindow.layout.v1";

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
#endif

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

QByteArray serializeDockLayout(QMainWindow *window)
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

bool restoreDockLayout(QMainWindow *window,
                       const QByteArray &layoutData,
                       QString *errorOut)
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

QJsonObject makeDockLayoutJson(QMainWindow *window)
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
            DockBackend::addDockWidgetCompat(window, dock, area);
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

QString layoutProfilesDirPath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty())
        return QString();
    return configDir + QStringLiteral("/layouts");
}

QString layoutProfilePath(const QString &profileName)
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

bool ensureLayoutProfilesDir(QString *errorOut)
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

bool saveLayoutProfile(QMainWindow *window,
                       const QString &profileName,
                       const QJsonObject &debugDockState,
                       const QJsonObject &coreDockConnections,
                       QString *errorOut)
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

bool restoreLayoutProfile(QMainWindow *window,
                          const QString &profileName,
                          QString *errorOut,
                          QJsonObject *debugDockStateOut,
                          QJsonObject *coreDockConnectionsOut)
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
