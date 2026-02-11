#include <QApplication>
#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSettings>
#include <QStringList>
#include <QWidget>

namespace {

QString dockAreaToString(Qt::DockWidgetArea area)
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

QSettings::Format settingsFormatForPath(const QString &path, const QString &explicitFormat)
{
    if (explicitFormat == QStringLiteral("ini"))
        return QSettings::IniFormat;
    if (explicitFormat == QStringLiteral("native"))
        return QSettings::NativeFormat;
    return path.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive)
        ? QSettings::IniFormat : QSettings::NativeFormat;
}

void addPlaceholderDock(QMainWindow *window, QList<QDockWidget *> *out, const QString &name)
{
    if (!window || !out)
        return;
    auto *dock = new QDockWidget(name, window);
    dock->setObjectName(name);
    dock->setWidget(new QWidget(dock));
    window->addDockWidget(Qt::RightDockWidgetArea, dock);
    out->append(dock);
}

QList<QDockWidget *> createPlaceholderDocks(QMainWindow *window, int extraHexCount, int maxExtraHex)
{
    QList<QDockWidget *> docks;
    const QStringList baseNames = {
        QStringLiteral("dockLCD"),
        QStringLiteral("dockControls"),
        QStringLiteral("dockFiles"),
        QStringLiteral("dockKeypad"),
        QStringLiteral("dockNandBrowser"),
        QStringLiteral("dockHwConfig"),
        QStringLiteral("dockDisasm"),
        QStringLiteral("dockRegisters"),
        QStringLiteral("dockStack"),
        QStringLiteral("dockMemory"),
        QStringLiteral("dockBreakpoints"),
        QStringLiteral("dockWatchpoints"),
        QStringLiteral("dockPortMonitor"),
        QStringLiteral("dockKeyHistory"),
        QStringLiteral("dockConsole"),
        QStringLiteral("dockMemVis"),
        QStringLiteral("dockCycleCounter"),
        QStringLiteral("dockTimerMonitor"),
        QStringLiteral("dockLCDState"),
        QStringLiteral("dockMMUViewer"),
    };
    for (const QString &name : baseNames)
        addPlaceholderDock(window, &docks, name);

    int dynamicCount = extraHexCount;
    if (dynamicCount < 0)
        dynamicCount = 0;
    if (dynamicCount > maxExtraHex)
        dynamicCount = maxExtraHex;

    for (int i = 1; i <= dynamicCount; ++i)
        addPlaceholderDock(window, &docks, QStringLiteral("dockMemory%1").arg(i));

    return docks;
}

QJsonObject exportFromWindow(const QMainWindow &window, const QList<QDockWidget *> &docks)
{
    QJsonArray dockArray;
    for (QDockWidget *dock : docks) {
        if (!dock)
            continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("objectName"), dock->objectName());
        obj.insert(QStringLiteral("title"), dock->windowTitle());
        obj.insert(QStringLiteral("visible"), dock->isVisible());
        obj.insert(QStringLiteral("floating"), dock->isFloating());
        obj.insert(QStringLiteral("area"), dockAreaToString(window.dockWidgetArea(dock)));
        obj.insert(QStringLiteral("geometryBase64"),
                   QString::fromLatin1(dock->saveGeometry().toBase64()));
        dockArray.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("docks"), dockArray);
    return root;
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("firebird-layout-convert"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Convert legacy Firebird QMainWindow layout to JSON"));
    parser.addHelpOption();

    QCommandLineOption settingsOpt(QStringList() << QStringLiteral("s") << QStringLiteral("settings"),
                                   QStringLiteral("Path to Firebird settings file (ini/native)."),
                                   QStringLiteral("path"));
    QCommandLineOption outputOpt(QStringList() << QStringLiteral("o") << QStringLiteral("output"),
                                 QStringLiteral("Write JSON output to file (defaults to stdout)."),
                                 QStringLiteral("path"));
    QCommandLineOption formatOpt(QStringList() << QStringLiteral("f") << QStringLiteral("format"),
                                 QStringLiteral("Settings format: auto|ini|native."),
                                 QStringLiteral("format"),
                                 QStringLiteral("auto"));
    QCommandLineOption versionOpt(QStringList() << QStringLiteral("v") << QStringLiteral("window-version"),
                                  QStringLiteral("Preferred QMainWindow state version."),
                                  QStringLiteral("version"),
                                  QStringLiteral("9"));
    QCommandLineOption noFallbackOpt(QStringList() << QStringLiteral("no-version-fallback"),
                                     QStringLiteral("Do not try older versions if restore fails."));
    QCommandLineOption extraHexOpt(QStringList() << QStringLiteral("extra-hex"),
                                   QStringLiteral("Override debug extra hex dock count."),
                                   QStringLiteral("count"),
                                   QStringLiteral("-1"));
    QCommandLineOption maxExtraHexOpt(QStringList() << QStringLiteral("max-extra-hex"),
                                      QStringLiteral("Maximum dynamic dockMemoryN placeholders to create."),
                                      QStringLiteral("count"),
                                      QStringLiteral("32"));
    QCommandLineOption prettyOpt(QStringList() << QStringLiteral("pretty"),
                                 QStringLiteral("Pretty-print output JSON."));

    parser.addOption(settingsOpt);
    parser.addOption(outputOpt);
    parser.addOption(formatOpt);
    parser.addOption(versionOpt);
    parser.addOption(noFallbackOpt);
    parser.addOption(extraHexOpt);
    parser.addOption(maxExtraHexOpt);
    parser.addOption(prettyOpt);
    parser.process(app);

    if (!parser.isSet(settingsOpt)) {
        parser.showHelp(2);
        return 2;
    }

    const QString settingsPath = parser.value(settingsOpt);
    const QString formatValue = parser.value(formatOpt).trimmed().toLower();
    if (formatValue != QStringLiteral("auto")
        && formatValue != QStringLiteral("ini")
        && formatValue != QStringLiteral("native")) {
        qCritical("Invalid --format value. Use auto, ini, or native.");
        return 2;
    }

    bool okVersion = false;
    const int preferredVersion = parser.value(versionOpt).toInt(&okVersion);
    if (!okVersion || preferredVersion < 1) {
        qCritical("Invalid --window-version value.");
        return 2;
    }

    bool okMaxExtra = false;
    const int maxExtraHex = parser.value(maxExtraHexOpt).toInt(&okMaxExtra);
    if (!okMaxExtra || maxExtraHex < 0) {
        qCritical("Invalid --max-extra-hex value.");
        return 2;
    }

    bool okExtraHex = false;
    const int extraHexOverride = parser.value(extraHexOpt).toInt(&okExtraHex);
    if (!okExtraHex) {
        qCritical("Invalid --extra-hex value.");
        return 2;
    }

    QSettings settings(settingsPath, settingsFormatForPath(settingsPath, formatValue));
    const QByteArray state = settings.value(QStringLiteral("windowState")).toByteArray();
    if (state.isEmpty()) {
        qCritical("No windowState found in settings.");
        return 1;
    }

    int extraHex = extraHexOverride;
    if (extraHex < 0)
        extraHex = settings.value(QStringLiteral("debugExtraHexDockCount"), 0).toInt();

    QMainWindow window;
    window.setDockOptions(QMainWindow::AllowTabbedDocks
                          | QMainWindow::AllowNestedDocks
                          | QMainWindow::AnimatedDocks
                          | QMainWindow::GroupedDragging);

    const QList<QDockWidget *> docks = createPlaceholderDocks(&window, extraHex, maxExtraHex);

    bool restoreOk = false;
    int usedVersion = preferredVersion;
    if (parser.isSet(noFallbackOpt)) {
        restoreOk = window.restoreState(state, preferredVersion);
    } else {
        for (int version = preferredVersion; version >= 1; --version) {
            if (window.restoreState(state, version)) {
                restoreOk = true;
                usedVersion = version;
                break;
            }
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("firebird.qmainwindow.layout.v1"));
    root.insert(QStringLiteral("sourceSettingsPath"), settingsPath);
    root.insert(QStringLiteral("windowStateBase64"), QString::fromLatin1(state.toBase64()));
    root.insert(QStringLiteral("preferredVersion"), preferredVersion);
    root.insert(QStringLiteral("usedVersion"), usedVersion);
    root.insert(QStringLiteral("restoreSucceeded"), restoreOk);
    root.insert(QStringLiteral("extraHexDocks"), extraHex);

    const QString existingJson = settings.value(QStringLiteral("windowLayoutJson")).toString();
    if (!existingJson.isEmpty()) {
        QJsonParseError err = {};
        const QJsonDocument existingDoc = QJsonDocument::fromJson(existingJson.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && existingDoc.isObject())
            root.insert(QStringLiteral("existingWindowLayoutJson"), existingDoc.object());
    }

    const QJsonObject docksJson = exportFromWindow(window, docks);
    root.insert(QStringLiteral("docks"), docksJson.value(QStringLiteral("docks")).toArray());

    const QJsonDocument outputDoc(root);
    const QJsonDocument::JsonFormat jsonFormat =
        parser.isSet(prettyOpt) ? QJsonDocument::Indented : QJsonDocument::Compact;
    const QByteArray outputBytes = outputDoc.toJson(jsonFormat);

    if (parser.isSet(outputOpt)) {
        QFile outFile(parser.value(outputOpt));
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical("Could not write output file.");
            return 1;
        }
        outFile.write(outputBytes);
        outFile.close();
    } else {
        QFile out;
        if (!out.open(stdout, QIODevice::WriteOnly))
            return 1;
        out.write(outputBytes);
        out.close();
    }

    return 0;
}
