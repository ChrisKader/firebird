#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QtTest/QtTest>
#include <QTemporaryDir>

namespace {

static QByteArray createLegacyWindowState(int version, int extraHexDocks)
{
    QMainWindow window;
    window.setDockOptions(QMainWindow::AllowTabbedDocks
                          | QMainWindow::AllowNestedDocks
                          | QMainWindow::AnimatedDocks
                          | QMainWindow::GroupedDragging);

    auto addDock = [&window](const QString &name, Qt::DockWidgetArea area) -> QDockWidget * {
        auto *dock = new QDockWidget(name, &window);
        dock->setObjectName(name);
        dock->setWidget(new QWidget(dock));
        window.addDockWidget(area, dock);
        return dock;
    };

    QDockWidget *dockLcd = addDock(QStringLiteral("dockLCD"), Qt::LeftDockWidgetArea);
    QDockWidget *dockControls = addDock(QStringLiteral("dockControls"), Qt::LeftDockWidgetArea);
    QDockWidget *dockDisasm = addDock(QStringLiteral("dockDisasm"), Qt::RightDockWidgetArea);
    QDockWidget *dockRegs = addDock(QStringLiteral("dockRegisters"), Qt::RightDockWidgetArea);
    QDockWidget *dockConsole = addDock(QStringLiteral("dockConsole"), Qt::BottomDockWidgetArea);

    window.tabifyDockWidget(dockLcd, dockControls);
    window.tabifyDockWidget(dockDisasm, dockRegs);
    dockLcd->raise();
    dockDisasm->raise();

    for (int i = 1; i <= extraHexDocks; ++i) {
        QDockWidget *dock = addDock(QStringLiteral("dockMemory%1").arg(i), Qt::BottomDockWidgetArea);
        window.tabifyDockWidget(dockConsole, dock);
    }
    dockConsole->raise();

    window.resize(1280, 800);
    return window.saveState(version);
}

} // namespace

class LayoutConvertTest : public QObject
{
    Q_OBJECT

private:
    QString m_converterPath;
    QStringList m_tempDirs;

    QString createSettingsFile(bool includeWindowState,
                               int stateVersion,
                               int extraHexDocks,
                               const QJsonObject &existingLayout = QJsonObject())
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid())
            return QString();
        tempDir.setAutoRemove(false);
        const QString basePath = tempDir.path();
        const QString settingsPath = basePath + QStringLiteral("/firebird-test.ini");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            if (includeWindowState)
                settings.setValue(QStringLiteral("windowState"), createLegacyWindowState(stateVersion, extraHexDocks));
            settings.setValue(QStringLiteral("debugExtraHexDockCount"), extraHexDocks);
            if (!existingLayout.isEmpty()) {
                const QByteArray json = QJsonDocument(existingLayout).toJson(QJsonDocument::Compact);
                settings.setValue(QStringLiteral("windowLayoutJson"), QString::fromUtf8(json));
            }
            settings.sync();
        }
        m_tempDirs.append(basePath);
        return settingsPath;
    }

    static QJsonObject parseJsonObject(const QByteArray &bytes)
    {
        QJsonParseError err = {};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return QJsonObject();
        return doc.object();
    }

    void runConverter(const QStringList &args, int *exitCode, QByteArray *stdOut, QByteArray *stdErr)
    {
        QVERIFY2(!m_converterPath.isEmpty(), "Layout converter path is empty");
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        proc.setProcessEnvironment(env);
        proc.setProgram(m_converterPath);
        proc.setArguments(args);
        proc.start();
        QVERIFY2(proc.waitForStarted(), "Layout converter failed to start");
        QVERIFY2(proc.waitForFinished(15000), "Layout converter timed out");
        if (exitCode)
            *exitCode = proc.exitCode();
        if (stdOut)
            *stdOut = proc.readAllStandardOutput();
        if (stdErr)
            *stdErr = proc.readAllStandardError();
    }

private slots:
    void initTestCase()
    {
        const QString fromEnv = qEnvironmentVariable("FIREBIRD_LAYOUT_CONVERTER");
        if (!fromEnv.isEmpty()) {
            m_converterPath = fromEnv;
        } else {
            const QString candidate = QCoreApplication::applicationDirPath()
                + QLatin1String("/firebird-layout-convert");
            if (QFileInfo::exists(candidate))
                m_converterPath = candidate;
        }

        QVERIFY2(!m_converterPath.isEmpty(), "Could not resolve firebird-layout-convert path");
        QVERIFY2(QFileInfo::exists(m_converterPath),
                 qPrintable(QStringLiteral("Converter binary not found: %1").arg(m_converterPath)));
    }

    void cleanupTestCase()
    {
        for (const QString &path : m_tempDirs)
            QDir(path).removeRecursively();
    }

    void converts_with_version_fallback()
    {
        const QString settingsPath = createSettingsFile(true, 7, 1);
        QVERIFY2(!settingsPath.isEmpty(), "Failed to create settings file");

        int exitCode = -1;
        QByteArray stdOut;
        QByteArray stdErr;
        runConverter({QStringLiteral("--settings"), settingsPath,
                      QStringLiteral("--window-version"), QStringLiteral("9")},
                     &exitCode, &stdOut, &stdErr);

        QCOMPARE(exitCode, 0);

        const QJsonObject root = parseJsonObject(stdOut);
        QVERIFY2(!root.isEmpty(), stdOut.constData());
        QCOMPARE(root.value(QStringLiteral("schema")).toString(),
                 QStringLiteral("firebird.qmainwindow.layout.v1"));
        QCOMPARE(root.value(QStringLiteral("preferredVersion")).toInt(), 9);
        QCOMPARE(root.value(QStringLiteral("usedVersion")).toInt(), 7);
        QCOMPARE(root.value(QStringLiteral("restoreSucceeded")).toBool(), true);
    }

    void no_fallback_mode_reports_failed_restore()
    {
        const QString settingsPath = createSettingsFile(true, 7, 0);
        QVERIFY2(!settingsPath.isEmpty(), "Failed to create settings file");

        int exitCode = -1;
        QByteArray stdOut;
        QByteArray stdErr;
        runConverter({QStringLiteral("--settings"), settingsPath,
                      QStringLiteral("--window-version"), QStringLiteral("9"),
                      QStringLiteral("--no-version-fallback")},
                     &exitCode, &stdOut, &stdErr);

        QCOMPARE(exitCode, 0);
        const QJsonObject root = parseJsonObject(stdOut);
        QVERIFY2(!root.isEmpty(), stdOut.constData());
        QCOMPARE(root.value(QStringLiteral("usedVersion")).toInt(), 9);
        QCOMPARE(root.value(QStringLiteral("restoreSucceeded")).toBool(), false);
    }

    void fails_when_window_state_missing()
    {
        const QString settingsPath = createSettingsFile(false, 9, 0);
        QVERIFY2(!settingsPath.isEmpty(), "Failed to create settings file");

        int exitCode = -1;
        QByteArray stdOut;
        QByteArray stdErr;
        runConverter({QStringLiteral("--settings"), settingsPath}, &exitCode, &stdOut, &stdErr);

        QCOMPARE(exitCode, 1);
        QVERIFY(stdOut.trimmed().isEmpty());
        QVERIFY(stdErr.contains("No windowState found in settings."));
    }

    void preserves_existing_layout_json()
    {
        QJsonObject existing;
        existing.insert(QStringLiteral("schema"), QStringLiteral("firebird.qmainwindow.layout.v1"));
        existing.insert(QStringLiteral("note"), QStringLiteral("legacy bridge"));
        const QString settingsPath = createSettingsFile(true, 9, 0, existing);
        QVERIFY2(!settingsPath.isEmpty(), "Failed to create settings file");

        int exitCode = -1;
        QByteArray stdOut;
        QByteArray stdErr;
        runConverter({QStringLiteral("--settings"), settingsPath}, &exitCode, &stdOut, &stdErr);

        QCOMPARE(exitCode, 0);
        const QJsonObject root = parseJsonObject(stdOut);
        QVERIFY2(!root.isEmpty(), stdOut.constData());
        QVERIFY(root.contains(QStringLiteral("existingWindowLayoutJson")));
        const QJsonObject bridge = root.value(QStringLiteral("existingWindowLayoutJson")).toObject();
        QCOMPARE(bridge.value(QStringLiteral("note")).toString(), QStringLiteral("legacy bridge"));
    }

    void exports_dynamic_hex_placeholders()
    {
        const QString settingsPath = createSettingsFile(true, 9, 2);
        QVERIFY2(!settingsPath.isEmpty(), "Failed to create settings file");

        int exitCode = -1;
        QByteArray stdOut;
        QByteArray stdErr;
        runConverter({QStringLiteral("--settings"), settingsPath,
                      QStringLiteral("--pretty")},
                     &exitCode, &stdOut, &stdErr);

        QCOMPARE(exitCode, 0);

        const QJsonObject root = parseJsonObject(stdOut);
        QVERIFY2(!root.isEmpty(), stdOut.constData());
        QCOMPARE(root.value(QStringLiteral("extraHexDocks")).toInt(), 2);
        QCOMPARE(root.value(QStringLiteral("restoreSucceeded")).toBool(), true);

        QSet<QString> names;
        const QJsonArray docks = root.value(QStringLiteral("docks")).toArray();
        for (const QJsonValue &v : docks) {
            const QString name = v.toObject().value(QStringLiteral("objectName")).toString();
            if (!name.isEmpty())
                names.insert(name);
        }

        QVERIFY(names.contains(QStringLiteral("dockMemory1")));
        QVERIFY(names.contains(QStringLiteral("dockMemory2")));
    }
};

QTEST_MAIN(LayoutConvertTest)

#include "layout_convert_test.moc"
