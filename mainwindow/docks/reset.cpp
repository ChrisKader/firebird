#include "mainwindow.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>

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

#include "mainwindow/docks/baselinelayout.h"
#include "debugger/dockmanager.h"
#include "mainwindow/docks/baseline.h"
#include "ui/dockbackend.h"
#include "ui/dockwidget.h"
#include "ui/kdockwidget.h"
#include "ui/materialicons.h"

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

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
static void addDockWidgetCompatWithAnyRelative(QMainWindow *window,
                                               DockWidget *dock,
                                               Qt::DockWidgetArea area,
                                               KDDockWidgets::QtWidgets::DockWidget *relativeTo = nullptr,
                                               bool startHidden = false,
                                               bool preserveCurrentSize = false,
                                               const QSize &preferredSize = QSize())
{
    if (!window || !dock)
        return;

    if (auto *kdd = asKDDMainWindow(window)) {
        KDDockWidgets::InitialOption initial;
        if (preferredSize.isValid() && preferredSize.width() > 0 && preferredSize.height() > 0)
            initial.preferredSize = preferredSize;
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
}
#endif

static void tabifyDockWidgetCompat(QMainWindow *window,
                                   DockWidget *first,
                                   DockWidget *second)
{
    if (!window || !first || !second || first == second)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (asKDDMainWindow(window)) {
        first->addDockWidgetAsTab(second);
        return;
    }
#else
    window->tabifyDockWidget(first, second);
#endif
}

void MainWindow::resetDockLayout()
{
    // Code-backed baseline layout sourced from typed C++ baseline rules.
    m_connectedCoreDockPairs.clear();
    m_coreDockDirectionalAreas.clear();

    auto dockByName = [this](const char *objectName) -> DockWidget * {
        if (!content_window || !objectName || !*objectName)
            return nullptr;
        return content_window->findChild<DockWidget *>(QString::fromLatin1(objectName));
    };
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    auto anyDockByName = [this](const char *objectName) -> KDDockWidgets::QtWidgets::DockWidget * {
        if (!content_window || !objectName || !*objectName)
            return nullptr;
        const QString name = QString::fromLatin1(objectName);
        if (auto *direct = content_window->findChild<KDDockWidgets::QtWidgets::DockWidget *>(name))
            return direct;
        const QList<KDDockWidgets::QtWidgets::DockWidget *> all =
            content_window->findChildren<KDDockWidgets::QtWidgets::DockWidget *>();
        for (KDDockWidgets::QtWidgets::DockWidget *dock : all) {
            if (!dock)
                continue;
            if (dock->objectName() == name || dock->uniqueName() == name)
                return dock;
        }
        return nullptr;
    };

    if (qEnvironmentVariableIsSet("FIREBIRD_DUMP_BASELINE_NAMES")) {
        qDebug("baseline reset: known KDD dock widgets:");
        const QList<KDDockWidgets::QtWidgets::DockWidget *> all =
            content_window->findChildren<KDDockWidgets::QtWidgets::DockWidget *>();
        for (KDDockWidgets::QtWidgets::DockWidget *dock : all) {
            if (!dock)
                continue;
            qDebug("  objectName=%s uniqueName=%s",
                   dock->objectName().toUtf8().constData(),
                   dock->uniqueName().toUtf8().constData());
        }
    }
#endif

    // Normalize all known docks first so a reset is deterministic regardless of current state.
    const QList<DockWidget *> allDocks =
        content_window ? content_window->findChildren<DockWidget *>() : QList<DockWidget *>();
    for (DockWidget *dock : allDocks) {
        if (!dock)
            continue;
        if (dock->isFloating())
            dock->setFloating(false);
        dock->setVisible(false);
    }

    if (m_debugDocks)
        m_debugDocks->resetLayout();

    bool baselineRestoredWithKdd = false;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    const QByteArray baselineLayoutBytes = makeBaselineKddLayoutBytes();
    if (!baselineLayoutBytes.isEmpty()) {
        QString restoreError;
        KDDockWidgets::LayoutSaver absoluteRestorer;
        baselineRestoredWithKdd = absoluteRestorer.restoreLayout(baselineLayoutBytes);
        if (!baselineRestoredWithKdd) {
            KDDockWidgets::LayoutSaver relativeRestorer(KDDockWidgets::RestoreOption_RelativeToMainWindow);
            baselineRestoredWithKdd = relativeRestorer.restoreLayout(baselineLayoutBytes);
        }
        if (!baselineRestoredWithKdd) {
            qWarning() << "Baseline restore via LayoutSaver failed, using manual fallback";
        }
    }
#endif

    if (!baselineRestoredWithKdd) {

    const auto decodedFrameById = [](const char *frameId)
            -> const BaselineLayout::DecodedFrameRule * {
        if (!frameId || !*frameId)
            return nullptr;
        for (const auto &frame : BaselineLayout::kDecodedFrameRules) {
            if (frame.frameId && qstrcmp(frame.frameId, frameId) == 0)
                return &frame;
        }
        return nullptr;
    };

    for (const BaselineLayout::DecodedPlacementRule &placement : BaselineLayout::kDecodedPlacementRules) {
        const BaselineLayout::DecodedFrameRule *frame = decodedFrameById(placement.frameId);
        if (!frame || frame->dockCount <= 0)
            continue;

        const char *primaryDockName = frame->dockWidgets[0];
        DockWidget *primary = dockByName(primaryDockName);
        if (!primary)
            continue;

        const QSize preferred(frame->width, frame->height);

        primary->setFloating(false);
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
        KDDockWidgets::QtWidgets::DockWidget *relativeTo = nullptr;
        if (const BaselineLayout::DecodedFrameRule *relativeFrame =
                decodedFrameById(placement.relativeFrameId)) {
            relativeTo = anyDockByName(relativeFrame->dockWidgets[0]);
        }
        addDockWidgetCompatWithAnyRelative(content_window,
                                           primary,
                                           placement.area,
                                           relativeTo,
                                           false,
                                           false,
                                           preferred);
#else
        DockWidget *relativeTo = nullptr;
        if (const BaselineLayout::DecodedFrameRule *relativeFrame =
                decodedFrameById(placement.relativeFrameId)) {
            relativeTo = dockByName(relativeFrame->dockWidgets[0]);
        }
        DockBackend::addDockWidgetCompat(content_window,
                                         primary,
                                         placement.area,
                                         relativeTo,
                                         false,
                                         false,
                                         preferred);
#endif
        primary->setVisible(true);

        std::array<DockWidget *, 4> frameDocks = {{ nullptr, nullptr, nullptr, nullptr }};
        int frameDockCount = 0;
        for (int i = 0; i < frame->dockCount && i < static_cast<int>(frame->dockWidgets.size()); ++i) {
            const char *dockName = frame->dockWidgets[i];
            if (!dockName || !*dockName)
                continue;
            DockWidget *dock = dockByName(dockName);
            if (!dock)
                continue;
            if (i > 0) {
                dock->setFloating(false);
                // Tab directly into the primary frame. Splitting first and then tabifying
                // perturbs KDD's splitter geometry and drifts away from baseline.
                tabifyDockWidgetCompat(content_window, primary, dock);
            }
            dock->setVisible(true);
            if (frameDockCount < static_cast<int>(frameDocks.size()))
                frameDocks[frameDockCount++] = dock;
        }

        if (frame->currentTabIndex >= 0 && frame->currentTabIndex < frameDockCount) {
            DockWidget *current = frameDocks[frame->currentTabIndex];
            if (current)
                current->raise();
        }
    }

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    // Size reconciliation pass using decoded tree x/y/width/height targets.
    for (int pass = 0; pass < 12; ++pass) {
        for (const BaselineLayout::DecodedLayoutNodeRule &node : BaselineLayout::kDecodedLayoutTree) {
            if (!node.frameId || !*node.frameId)
                continue;
            const BaselineLayout::DecodedFrameRule *frame = decodedFrameById(node.frameId);
            if (!frame || frame->dockCount <= 0)
                continue;
            if (frame->dockWidgets[0] &&
                qstrcmp(frame->dockWidgets[0], "-persistentCentralDockWidget") == 0) {
                continue;
            }

            DockWidget *dock = dockByName(frame->dockWidgets[0]);
            if (!dock)
                continue;
            KDDockWidgets::Core::DockWidget *coreDock = dock->dockWidget();
            if (!coreDock)
                continue;

            const QRect current = coreDock->groupGeometry();
            const QRect target(node.x, node.y, node.width, node.height);
            if (!current.isValid() || !target.isValid())
                continue;

            const int leftDelta = current.left() - target.left();
            const int topDelta = current.top() - target.top();
            const int rightDelta = target.right() - current.right();
            const int bottomDelta = target.bottom() - current.bottom();
            if (leftDelta != 0 || topDelta != 0 || rightDelta != 0 || bottomDelta != 0) {
                coreDock->resizeInLayout(leftDelta, topDelta, rightDelta, bottomDelta);
            }
        }
    }
#endif
    }

    if (m_debugDocks)
        m_debugDocks->restoreDockStates(makeBaselineDebugDockStateObject());

    QSet<QString> closedDockNames;
    for (const char *closedDockName : BaselineLayout::kClosedDockWidgetNames) {
        if (closedDockName && *closedDockName)
            closedDockNames.insert(QString::fromLatin1(closedDockName));
    }

    QSet<QString> dockProfileNames;
    for (const BaselineLayout::DockProfileEntry &stateRule : BaselineLayout::kDockProfileEntries) {
        if (!stateRule.objectName)
            continue;
        const QString dockName = QString::fromLatin1(stateRule.objectName);
        dockProfileNames.insert(dockName);
        DockWidget *dock = content_window
                ? content_window->findChild<DockWidget *>(dockName)
                : nullptr;
        if (!dock)
            continue;
        if (!baselineRestoredWithKdd) {
            if (stateRule.geometry.width > 0 && stateRule.geometry.height > 0)
                dock->resize(stateRule.geometry.width, stateRule.geometry.height);
            dock->setFloating(stateRule.floating);
            dock->setVisible(stateRule.visible && !closedDockNames.contains(dockName));
        }
        if (stateRule.title)
            dock->setWindowTitle(QString::fromLatin1(stateRule.title));
    }

    if (!baselineRestoredWithKdd) {
        for (const BaselineLayout::AllDockWidgetRule &dockRule : BaselineLayout::kAllDockWidgetRules) {
            if (!dockRule.uniqueName)
                continue;
            const QString dockName = QString::fromLatin1(dockRule.uniqueName);
            if (dockProfileNames.contains(dockName))
                continue;
            DockWidget *dock = content_window
                    ? content_window->findChild<DockWidget *>(dockName)
                    : nullptr;
            if (!dock)
                continue;

            const BaselineLayout::RectRule &floatingGeometry = dockRule.lastPosition.lastFloatingGeometry;
            if (dockRule.lastPosition.wasFloating &&
                floatingGeometry.width > 0 &&
                floatingGeometry.height > 0) {
                dock->setFloating(true);
                dock->setGeometry(QRect(floatingGeometry.x,
                                        floatingGeometry.y,
                                        floatingGeometry.width,
                                        floatingGeometry.height));
            } else {
                dock->setFloating(false);
            }
            dock->setVisible(!closedDockNames.contains(dockName));
        }
    }

    restoreCoreDockConnections(makeBaselineCoreDockConnectionsObject());
    scheduleCoreDockConnectOverlayRefresh();
    scheduleLayoutHistoryCapture();
}
