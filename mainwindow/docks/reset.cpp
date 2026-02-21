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

#include "app/baselinelayout.h"
#include "debugger/dockmanager.h"
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

[[maybe_unused]] static void addDockWidgetCompat(QMainWindow *window,
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

static QString coreDockAreaToString(Qt::DockWidgetArea area);

static QJsonObject baselineRectObject(const BaselineLayout::RectRule &rect)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), rect.x);
    object.insert(QStringLiteral("y"), rect.y);
    object.insert(QStringLiteral("width"), rect.width);
    object.insert(QStringLiteral("height"), rect.height);
    return object;
}

static QJsonObject baselineSizeObject(const BaselineLayout::SizeRule &size)
{
    QJsonObject object;
    object.insert(QStringLiteral("width"), size.width);
    object.insert(QStringLiteral("height"), size.height);
    return object;
}

static QJsonObject baselineSizingInfoObject(const BaselineLayout::DecodedLayoutNodeRule &node)
{
    QJsonObject object;
    object.insert(QStringLiteral("geometry"),
                  baselineRectObject({ node.x, node.y, node.width, node.height }));
    object.insert(QStringLiteral("minSize"),
                  baselineSizeObject({ node.minWidth, node.minHeight }));
    object.insert(QStringLiteral("maxSizeHint"),
                  baselineSizeObject({ node.maxWidth, node.maxHeight }));
    object.insert(QStringLiteral("percentageWithinParent"), node.percentageWithinParent);
    return object;
}

static QJsonObject baselineLayoutTreeNodeObject(int index)
{
    if (index < 0 || index >= static_cast<int>(BaselineLayout::kDecodedLayoutTree.size()))
        return QJsonObject();

    const BaselineLayout::DecodedLayoutNodeRule &node = BaselineLayout::kDecodedLayoutTree[index];
    QJsonObject object;
    object.insert(QStringLiteral("isContainer"), node.isContainer);
    object.insert(QStringLiteral("isVisible"), node.isVisible);
    object.insert(QStringLiteral("sizingInfo"), baselineSizingInfoObject(node));

    if (node.isContainer) {
        object.insert(QStringLiteral("orientation"), node.orientation);
        QJsonArray children;
        for (int i = 0; i < node.childCount && i < static_cast<int>(node.children.size()); ++i) {
            const int childIndex = node.children[i];
            if (childIndex >= 0)
                children.append(baselineLayoutTreeNodeObject(childIndex));
        }
        object.insert(QStringLiteral("children"), children);
    } else if (node.frameId && *node.frameId) {
        object.insert(QStringLiteral("guestId"), QString::fromLatin1(node.frameId));
    }

    return object;
}

static QJsonObject makeBaselineKddLayoutObject()
{
    QJsonObject layoutRoot;
    layoutRoot.insert(QStringLiteral("serializationVersion"), BaselineLayout::kSerializationVersion);

    QJsonArray screenInfo;
    for (const BaselineLayout::ScreenInfoRule &rule : BaselineLayout::kScreenInfoRules) {
        if (!rule.name)
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("index"), rule.index);
        entry.insert(QStringLiteral("name"), QString::fromLatin1(rule.name));
        entry.insert(QStringLiteral("devicePixelRatio"), rule.devicePixelRatio);
        entry.insert(QStringLiteral("geometry"), baselineRectObject(rule.geometry));
        screenInfo.append(entry);
    }
    layoutRoot.insert(QStringLiteral("screenInfo"), screenInfo);

    QJsonArray allDockWidgets;
    for (const BaselineLayout::AllDockWidgetRule &rule : BaselineLayout::kAllDockWidgetRules) {
        if (!rule.uniqueName)
            continue;
        QJsonObject dockWidget;
        dockWidget.insert(QStringLiteral("uniqueName"), QString::fromLatin1(rule.uniqueName));
        dockWidget.insert(QStringLiteral("lastCloseReason"), rule.lastCloseReason);

        QJsonObject lastPosition;
        lastPosition.insert(QStringLiteral("lastFloatingGeometry"),
                            baselineRectObject(rule.lastPosition.lastFloatingGeometry));
        lastPosition.insert(QStringLiteral("lastOverlayedGeometries"), QJsonArray());
        lastPosition.insert(QStringLiteral("tabIndex"), rule.lastPosition.tabIndex);
        lastPosition.insert(QStringLiteral("wasFloating"), rule.lastPosition.wasFloating);

        QJsonArray placeholders;
        if (rule.lastPosition.placeholderCount > 0) {
            QJsonObject placeholder;
            placeholder.insert(QStringLiteral("isFloatingWindow"), rule.lastPosition.placeholder.isFloatingWindow);
            placeholder.insert(QStringLiteral("itemIndex"), rule.lastPosition.placeholder.itemIndex);
            if (rule.lastPosition.placeholder.mainWindowUniqueName) {
                placeholder.insert(QStringLiteral("mainWindowUniqueName"),
                                   QString::fromLatin1(rule.lastPosition.placeholder.mainWindowUniqueName));
            }
            placeholders.append(placeholder);
        }
        lastPosition.insert(QStringLiteral("placeholders"), placeholders);
        dockWidget.insert(QStringLiteral("lastPosition"), lastPosition);
        allDockWidgets.append(dockWidget);
    }
    layoutRoot.insert(QStringLiteral("allDockWidgets"), allDockWidgets);

    QJsonArray closedDockWidgets;
    for (const char *name : BaselineLayout::kClosedDockWidgetNames) {
        if (name && *name)
            closedDockWidgets.append(QString::fromLatin1(name));
    }
    layoutRoot.insert(QStringLiteral("closedDockWidgets"), closedDockWidgets);

    layoutRoot.insert(QStringLiteral("floatingWindows"), QJsonArray());

    QJsonArray mainWindows;
    for (const BaselineLayout::MainWindowRule &mainWindowRule : BaselineLayout::kMainWindowRules) {
        if (!mainWindowRule.uniqueName)
            continue;
        QJsonObject mainWindow;
        mainWindow.insert(QStringLiteral("options"), mainWindowRule.options);
        mainWindow.insert(QStringLiteral("uniqueName"), QString::fromLatin1(mainWindowRule.uniqueName));
        mainWindow.insert(QStringLiteral("geometry"), baselineRectObject(mainWindowRule.geometry));
        mainWindow.insert(QStringLiteral("normalGeometry"), baselineRectObject(mainWindowRule.normalGeometry));
        mainWindow.insert(QStringLiteral("screenIndex"), mainWindowRule.screenIndex);
        mainWindow.insert(QStringLiteral("screenSize"), baselineSizeObject(mainWindowRule.screenSize));
        mainWindow.insert(QStringLiteral("isVisible"), mainWindowRule.isVisible);
        mainWindow.insert(QStringLiteral("affinities"), QJsonArray());
        mainWindow.insert(QStringLiteral("windowState"), mainWindowRule.windowState);

        QJsonObject multiSplitterLayout;
        multiSplitterLayout.insert(QStringLiteral("layout"),
                                   baselineLayoutTreeNodeObject(BaselineLayout::kDecodedLayoutRootNodeIndex));
        QJsonObject frames;
        for (const BaselineLayout::DecodedFrameRule &frame : BaselineLayout::kDecodedFrameRules) {
            if (!frame.frameId || !*frame.frameId || !frame.objectName || !*frame.objectName)
                continue;
            QJsonObject frameObject;
            frameObject.insert(QStringLiteral("id"), QString::fromLatin1(frame.frameId));
            frameObject.insert(QStringLiteral("isNull"), frame.isNull);
            frameObject.insert(QStringLiteral("objectName"), QString::fromLatin1(frame.objectName));
            frameObject.insert(QStringLiteral("geometry"),
                               baselineRectObject({ frame.x, frame.y, frame.width, frame.height }));
            frameObject.insert(QStringLiteral("options"), frame.options);
            frameObject.insert(QStringLiteral("currentTabIndex"), frame.currentTabIndex);
            if (frame.mainWindowUniqueName) {
                frameObject.insert(QStringLiteral("mainWindowUniqueName"),
                                   QString::fromLatin1(frame.mainWindowUniqueName));
            }

            QJsonArray dockWidgetsForFrame;
            for (int i = 0; i < frame.dockCount && i < static_cast<int>(frame.dockWidgets.size()); ++i) {
                const char *dockName = frame.dockWidgets[i];
                if (dockName && *dockName)
                    dockWidgetsForFrame.append(QString::fromLatin1(dockName));
            }
            frameObject.insert(QStringLiteral("dockWidgets"), dockWidgetsForFrame);
            frames.insert(QString::fromLatin1(frame.frameId), frameObject);
        }
        multiSplitterLayout.insert(QStringLiteral("frames"), frames);
        mainWindow.insert(QStringLiteral("multiSplitterLayout"), multiSplitterLayout);
        mainWindows.append(mainWindow);
    }
    layoutRoot.insert(QStringLiteral("mainWindows"), mainWindows);
    return layoutRoot;
}

static QByteArray makeBaselineKddLayoutBytes()
{
    return QJsonDocument(makeBaselineKddLayoutObject()).toJson(QJsonDocument::Compact);
}

static QJsonObject makeBaselineDebugDockStateObject()
{
    QJsonArray docks;

    for (const BaselineLayout::DebugDockStateRule &rule : BaselineLayout::kDebugDockStateRules) {
        if (!rule.dockId)
            continue;

        QJsonObject customState;
        const auto addInt = [&customState](const char *key, int value) {
            if (value != BaselineLayout::kUnsetInt)
                customState.insert(QString::fromLatin1(key), value);
        };
        const auto addBoolFlag = [&customState](const char *key, int value) {
            if (value == 0 || value == 1)
                customState.insert(QString::fromLatin1(key), value == 1);
        };
        const auto addString = [&customState](const char *key, const char *value) {
            if (value)
                customState.insert(QString::fromLatin1(key), QString::fromLatin1(value));
        };

        addString("baseAddr", rule.baseAddr);
        addString("searchText", rule.searchText);
        addInt("displayFormat", rule.displayFormat);
        addInt("modeIndex", rule.modeIndex);
        addInt("searchType", rule.searchType);
        addInt("selectedOffset", rule.selectedOffset);
        addBoolFlag("showAscii", rule.showAscii);
        addString("filterText", rule.filterText);
        addInt("fontSize", rule.fontSize);
        if (rule.includeEmptyCommandHistory)
            customState.insert(QStringLiteral("commandHistory"), QJsonArray());
        addInt("maxBlockCount", rule.maxBlockCount);
        addBoolFlag("autoRefresh", rule.autoRefresh);
        addInt("bpp", rule.bpp);
        addInt("imageHeight", rule.imageHeight);
        addInt("imageWidth", rule.imageWidth);
        addInt("zoom", rule.zoom);
        addInt("refreshIndex", rule.refreshIndex);

        QJsonObject item;
        item.insert(QStringLiteral("dockId"), QString::fromLatin1(rule.dockId));
        item.insert(QStringLiteral("customState"), customState);
        docks.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(BaselineLayout::kDebugDockStateSchema));
    root.insert(QStringLiteral("docks"), docks);
    return root;
}

static QJsonObject makeBaselineCoreDockConnectionsObject()
{
    QJsonArray pairs;
    for (const BaselineLayout::CoreDockConnectionRule &rule : BaselineLayout::kCoreDockConnectionRules) {
        if (!rule.a || !rule.b)
            continue;
        QJsonObject pair;
        pair.insert(QStringLiteral("a"), QString::fromLatin1(rule.a));
        pair.insert(QStringLiteral("b"), QString::fromLatin1(rule.b));
        pair.insert(QStringLiteral("area"), coreDockAreaToString(rule.area));
        pairs.append(pair);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(BaselineLayout::kCoreDockConnectionsSchema));
    root.insert(QStringLiteral("pairs"), pairs);
    return root;
}
static QString coreDockAreaToString(Qt::DockWidgetArea area)
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
        addDockWidgetCompat(content_window,
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
