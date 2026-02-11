#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFileInfo>
#include <QUuid>
#include <QCoreApplication>
#include <QWidget>

#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/LayoutSaver.h>

#include "ui/kdockwidget.h"

namespace {

QString uniqueName(const QString &prefix)
{
    return prefix + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128);
}

struct DockFixture {
    KDDockWidgets::QtWidgets::MainWindow *window = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *dockA = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *dockB = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *dockC = nullptr;

    DockFixture()
    {
        window = new KDDockWidgets::QtWidgets::MainWindow(
            uniqueName(QStringLiteral("mainWindow")),
            KDDockWidgets::MainWindowOption_HasCentralWidget);
        window->setPersistentCentralWidget(new QWidget(window));

        dockA = new KDDockWidgets::QtWidgets::DockWidget(uniqueName(QStringLiteral("dockA")));
        dockA->setTitle(QStringLiteral("Dock A"));
        dockA->setWidget(new QWidget());

        dockB = new KDDockWidgets::QtWidgets::DockWidget(uniqueName(QStringLiteral("dockB")));
        dockB->setTitle(QStringLiteral("Dock B"));
        dockB->setWidget(new QWidget());

        dockC = new KDDockWidgets::QtWidgets::DockWidget(uniqueName(QStringLiteral("dockC")));
        dockC->setTitle(QStringLiteral("Dock C"));
        dockC->setWidget(new QWidget());

        window->addDockWidget(dockA, KDDockWidgets::Location_OnRight);
        window->addDockWidget(dockB, KDDockWidgets::Location_OnBottom, dockA);
        dockA->addDockWidgetAsTab(dockC);

        window->resize(900, 700);
        window->show();
        QCoreApplication::processEvents();
    }

    ~DockFixture()
    {
        delete window;
    }
};

} // namespace

class KddLayoutTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    }

    void roundTripRestore()
    {
        DockFixture fixture;
        KDDockWidgets::LayoutSaver saver;

        const QByteArray baseline = saver.serializeLayout();
        QVERIFY2(!baseline.isEmpty(), "Expected non-empty serialized layout");

        fixture.dockB->setFloating(true);
        fixture.dockB->close();
        QCoreApplication::processEvents();

        QVERIFY2(saver.restoreLayout(baseline), "Layout restore from byte array failed");
        QCoreApplication::processEvents();

        QVERIFY(fixture.dockB->isOpen());
        QVERIFY(!fixture.dockB->isFloating());
    }

    void profileSwitching()
    {
        DockFixture fixture;
        KDDockWidgets::LayoutSaver saver;

        const QByteArray profileA = saver.serializeLayout();
        QVERIFY(!profileA.isEmpty());

        fixture.dockC->close();
        QCoreApplication::processEvents();
        QVERIFY(!fixture.dockC->isOpen());

        const QByteArray profileB = saver.serializeLayout();
        QVERIFY(!profileB.isEmpty());

        QVERIFY(saver.restoreLayout(profileA));
        QCoreApplication::processEvents();
        QVERIFY(fixture.dockC->isOpen());

        QVERIFY(saver.restoreLayout(profileB));
        QCoreApplication::processEvents();
        QVERIFY(!fixture.dockC->isOpen());
    }

    void fileRestoreAndCorruption()
    {
        DockFixture fixture;
        KDDockWidgets::LayoutSaver saver;
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString filePath = tempDir.path() + QStringLiteral("/layout.json");
        QVERIFY2(saver.saveToFile(filePath), "Failed to save layout profile to file");
        QVERIFY(QFileInfo::exists(filePath));

        fixture.dockA->close();
        QCoreApplication::processEvents();
        QVERIFY(!fixture.dockA->isOpen());

        QVERIFY2(saver.restoreFromFile(filePath), "Failed to restore layout profile from file");
        QCoreApplication::processEvents();
        QVERIFY(fixture.dockA->isOpen());

        const QByteArray corrupt = QByteArrayLiteral("{\"schema\":\"invalid\"}");
        QVERIFY(!saver.restoreLayout(corrupt));
    }

    void restoreRelativeToMainWindow()
    {
        DockFixture fixture;
        KDDockWidgets::LayoutSaver saver;
        const QByteArray baseline = saver.serializeLayout();
        QVERIFY(!baseline.isEmpty());

        fixture.window->move(2200, 200);
        fixture.dockB->setFloating(true);
        fixture.dockB->close();
        QCoreApplication::processEvents();

        KDDockWidgets::LayoutSaver relativeRestorer(KDDockWidgets::RestoreOption_RelativeToMainWindow);
        QVERIFY(relativeRestorer.restoreLayout(baseline));
        QCoreApplication::processEvents();

        QVERIFY(fixture.dockB->isOpen());
    }

    void wrapperCompatibility()
    {
        KDockWidget dock(uniqueName(QStringLiteral("compatDock")), QStringLiteral("Compat"));
        dock.setWidget(new QWidget());

        dock.setFeatures(QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
        QVERIFY((dock.options() & KDDockWidgets::DockWidgetOption_NotClosable) != 0);

        dock.setFeatures(QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
        QVERIFY((dock.options() & KDDockWidgets::DockWidgetOption_NotClosable) == 0);

        dock.setFeatures(QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetMovable);
        QVERIFY(dock.floatAction() != nullptr);
        QVERIFY(!dock.floatAction()->isEnabled());

        dock.setFeatures(QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
        QVERIFY(dock.floatAction()->isEnabled());

        dock.setAllowedAreas(Qt::NoDockWidgetArea);
        dock.setAllowedAreas(Qt::AllDockWidgetAreas);
    }
};

QTEST_MAIN(KddLayoutTest)
#include "kdd_layout_test.moc"
