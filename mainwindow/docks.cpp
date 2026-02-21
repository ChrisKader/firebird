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


#ifdef FIREBIRD_USE_KDDOCKWIDGETS
static QWidget *kddTitleBarWidgetForDock(DockWidget *dock)
{
    if (!dock)
        return nullptr;

    if (auto *kDock = dynamic_cast<KDockWidget *>(dock)) {
        if (auto *titleBar = kDock->actualTitleBar()) {
            if (auto *view = titleBar->view()) {
                return KDDockWidgets::QtCommon::View_qt::asQWidget(view);
            }
        }
    }

    return nullptr;
}
#endif

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

static Qt::DockWidgetArea coreDockAreaFromString(const QString &name)
{
    if (name == QLatin1String("left"))
        return Qt::LeftDockWidgetArea;
    if (name == QLatin1String("right"))
        return Qt::RightDockWidgetArea;
    if (name == QLatin1String("top"))
        return Qt::TopDockWidgetArea;
    if (name == QLatin1String("bottom"))
        return Qt::BottomDockWidgetArea;
    return Qt::NoDockWidgetArea;
}

void MainWindow::applyStandardDockFeatures(DockWidget *dw, bool closable) const
{
    if (!dw)
        return;
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    QDockWidget::DockWidgetFeatures features = QDockWidget::DockWidgetMovable |
                                               QDockWidget::DockWidgetFloatable;
    if (closable)
        features |= QDockWidget::DockWidgetClosable;
    dw->setFeatures(features);
}

DockWidget *MainWindow::createMainDock(const QString &title,
                                       QWidget *widget,
                                       const QString &objectName,
                                       Qt::DockWidgetArea area,
                                       QMenu *docksMenu,
                                       const QIcon &icon,
                                       bool hideTitlebar,
                                       bool closable,
                                       bool startVisible)
{
    const QString uniqueName = objectName.isEmpty() ? title : objectName;
    auto *dw = new KDockWidget(uniqueName, title, content_window);
    if (hideTitlebar)
        dw->applyThinTitlebar(true);
    if (!icon.isNull())
        dw->setWindowIcon(icon);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw, closable);
    addDockWidgetCompat(content_window, dw, area, nullptr, !startVisible);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
    if (!startVisible)
        dw->hide();
#endif
    if (docksMenu) {
        QAction *action = dw->toggleViewAction();
        if (!icon.isNull())
            action->setIcon(icon);
        docksMenu->addAction(action);
    }
    return dw;
}

QList<DockWidget *> MainWindow::coreGroupableDocks() const
{
    QList<DockWidget *> docks;
    for (DockWidget *dock : {m_dock_lcd, m_dock_controls, m_dock_keypad}) {
        if (dock)
            docks.append(dock);
    }
    return docks;
}

void MainWindow::refreshCoreDockWatchTargets()
{
    QHash<QObject *, QPointer<DockWidget>> desired;
    for (DockWidget *dock : coreGroupableDocks()) {
        if (!dock)
            continue;
        desired.insert(dock, dock);
        if (dock->isFloating()) {
            QWidget *floatingWindow = static_cast<QWidget *>(dock)->window();
            if (floatingWindow && floatingWindow != dock && !desired.contains(floatingWindow))
                desired.insert(floatingWindow, dock);
        }
    }

    for (auto it = m_coreDockWatchTargets.begin(); it != m_coreDockWatchTargets.end();) {
        QObject *obj = it.key();
        if (!obj || !desired.contains(obj)) {
            if (obj)
                obj->removeEventFilter(this);
            it = m_coreDockWatchTargets.erase(it);
            continue;
        }
        ++it;
    }

    for (auto it = desired.begin(); it != desired.end(); ++it) {
        QObject *obj = it.key();
        if (!obj)
            continue;
        if (!m_coreDockWatchTargets.contains(obj))
            obj->installEventFilter(this);
        m_coreDockWatchTargets.insert(obj, it.value());
    }
}

QString MainWindow::makeCorePairKey(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty() || a == b)
        return QString();
    return (a < b) ? (a + QLatin1Char('|') + b) : (b + QLatin1Char('|') + a);
}

QString MainWindow::makeCoreDirectionalKey(const QString &from, const QString &to)
{
    if (from.isEmpty() || to.isEmpty())
        return QString();
    return from + QStringLiteral("->") + to;
}

Qt::DockWidgetArea MainWindow::oppositeArea(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return Qt::RightDockWidgetArea;
    case Qt::RightDockWidgetArea: return Qt::LeftDockWidgetArea;
    case Qt::TopDockWidgetArea: return Qt::BottomDockWidgetArea;
    case Qt::BottomDockWidgetArea: return Qt::TopDockWidgetArea;
    default: return Qt::NoDockWidgetArea;
    }
}

QJsonObject MainWindow::serializeCoreDockConnections() const
{
    QJsonArray pairs;

    for (const QString &pairKey : std::as_const(m_connectedCoreDockPairs)) {
        const QStringList names = pairKey.split(QLatin1Char('|'));
        if (names.size() != 2)
            continue;

        const QString a = names.at(0);
        const QString b = names.at(1);
        const Qt::DockWidgetArea area = m_coreDockDirectionalAreas.value(
            makeCoreDirectionalKey(a, b),
            Qt::NoDockWidgetArea);

        QJsonObject pair;
        pair.insert(QStringLiteral("a"), a);
        pair.insert(QStringLiteral("b"), b);
        pair.insert(QStringLiteral("area"), coreDockAreaToString(area));
        pairs.append(pair);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("firebird.core.connections.v1"));
    root.insert(QStringLiteral("pairs"), pairs);
    return root;
}

void MainWindow::restoreCoreDockConnections(const QJsonObject &stateRoot)
{
    m_connectedCoreDockPairs.clear();
    m_coreDockDirectionalAreas.clear();

    if (stateRoot.isEmpty()) {
        scheduleCoreDockConnectOverlayRefresh();
        return;
    }

    const QList<DockWidget *> groupable = coreGroupableDocks();
    auto findDockByName = [groupable](const QString &name) -> DockWidget * {
        for (DockWidget *dock : groupable) {
            if (dock && dock->objectName() == name)
                return dock;
        }
        return nullptr;
    };

    const QJsonArray pairs = stateRoot.value(QStringLiteral("pairs")).toArray();
    for (const QJsonValue &value : pairs) {
        if (!value.isObject())
            continue;
        const QJsonObject pair = value.toObject();
        const QString aName = pair.value(QStringLiteral("a")).toString();
        const QString bName = pair.value(QStringLiteral("b")).toString();
        DockWidget *a = findDockByName(aName);
        DockWidget *b = findDockByName(bName);
        if (!a || !b || a == b)
            continue;
        const Qt::DockWidgetArea area =
            coreDockAreaFromString(pair.value(QStringLiteral("area")).toString());
        setCoreDockPairConnected(a, b, true, area);
    }

    applyConnectedCoreDocks(nullptr, false);
    scheduleCoreDockConnectOverlayRefresh();
}

void MainWindow::scheduleCoreDockConnectOverlayRefresh()
{
    refreshCoreDockWatchTargets();
    if (!m_coreDockOverlayTimer) {
        m_coreDockOverlayTimer = new QTimer(this);
        m_coreDockOverlayTimer->setSingleShot(true);
        m_coreDockOverlayTimer->setInterval(0);
        connect(m_coreDockOverlayTimer, &QTimer::timeout, this, &MainWindow::refreshCoreDockConnectOverlay);
    }
    m_coreDockOverlayTimer->start();
}

Qt::DockWidgetArea MainWindow::inferRelativeArea(DockWidget *from,
                                                  DockWidget *to,
                                                  QPoint *borderCenterOut) const
{
    if (!from || !to || from == to || !from->isVisible() || !to->isVisible() ||
        from->isFloating() || to->isFloating()) {
        if (borderCenterOut)
            *borderCenterOut = QPoint();
        return Qt::NoDockWidgetArea;
    }

    const QRect a(from->mapToGlobal(QPoint(0, 0)), from->size());
    const QRect b(to->mapToGlobal(QPoint(0, 0)), to->size());
    if (!a.isValid() || !b.isValid()) {
        if (borderCenterOut)
            *borderCenterOut = QPoint();
        return Qt::NoDockWidgetArea;
    }

    struct Candidate {
        Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
        int gap = INT_MAX;
        int overlap = 0;
        QPoint centerGlobal;
    };

    const int maxDim = qMax(qMax(a.width(), a.height()), qMax(b.width(), b.height()));
    const int tol = qBound(12, maxDim / 6, 48);
    const int minOverlap = qMax(18, qMin(qMin(a.width(), a.height()), qMin(b.width(), b.height())) / 4);
    Candidate best;

    auto consider = [&](Qt::DockWidgetArea area, int gap, int overlap, const QPoint &centerGlobal) {
        if (overlap < minOverlap || gap > tol)
            return;
        if (best.area == Qt::NoDockWidgetArea || gap < best.gap ||
            (gap == best.gap && overlap > best.overlap)) {
            best.area = area;
            best.gap = gap;
            best.overlap = overlap;
            best.centerGlobal = centerGlobal;
        }
    };

    const int verticalTop = qMax(a.top(), b.top());
    const int verticalBottom = qMin(a.bottom(), b.bottom());
    const int verticalOverlap = verticalBottom - verticalTop + 1;
    const int horizontalLeft = qMax(a.left(), b.left());
    const int horizontalRight = qMin(a.right(), b.right());
    const int horizontalOverlap = horizontalRight - horizontalLeft + 1;

    consider(Qt::RightDockWidgetArea,
             qAbs((a.right() + 1) - b.left()),
             verticalOverlap,
             QPoint((a.right() + b.left()) / 2, (verticalTop + verticalBottom) / 2));
    consider(Qt::LeftDockWidgetArea,
             qAbs(a.left() - (b.right() + 1)),
             verticalOverlap,
             QPoint((a.left() + b.right()) / 2, (verticalTop + verticalBottom) / 2));
    consider(Qt::BottomDockWidgetArea,
             qAbs((a.bottom() + 1) - b.top()),
             horizontalOverlap,
             QPoint((horizontalLeft + horizontalRight) / 2, (a.bottom() + b.top()) / 2));
    consider(Qt::TopDockWidgetArea,
             qAbs(a.top() - (b.bottom() + 1)),
             horizontalOverlap,
             QPoint((horizontalLeft + horizontalRight) / 2, (a.top() + b.bottom()) / 2));

    if (best.area == Qt::NoDockWidgetArea) {
        if (borderCenterOut)
            *borderCenterOut = QPoint();
        return Qt::NoDockWidgetArea;
    }

    if (borderCenterOut) {
        if (content_window)
            *borderCenterOut = content_window->mapFromGlobal(best.centerGlobal);
        else
            *borderCenterOut = mapFromGlobal(best.centerGlobal);
    }
    return best.area;
}

bool MainWindow::isCoreDockPairConnectedByName(const QString &nameA, const QString &nameB) const
{
    const QString pairKey = makeCorePairKey(nameA, nameB);
    return !pairKey.isEmpty() && m_connectedCoreDockPairs.contains(pairKey);
}

bool MainWindow::isCoreDockPairConnected(DockWidget *a, DockWidget *b) const
{
    if (!a || !b)
        return false;
    return isCoreDockPairConnectedByName(a->objectName(), b->objectName());
}

void MainWindow::setCoreDockPairConnected(DockWidget *a,
                                          DockWidget *b,
                                          bool connected,
                                          Qt::DockWidgetArea areaHint)
{
    if (!a || !b)
        return;

    const QString pairKey = makeCorePairKey(a->objectName(), b->objectName());
    if (pairKey.isEmpty())
        return;

    const QString aToB = makeCoreDirectionalKey(a->objectName(), b->objectName());
    const QString bToA = makeCoreDirectionalKey(b->objectName(), a->objectName());

    if (!connected) {
        m_connectedCoreDockPairs.remove(pairKey);
        m_coreDockDirectionalAreas.remove(aToB);
        m_coreDockDirectionalAreas.remove(bToA);
        return;
    }

    m_connectedCoreDockPairs.insert(pairKey);
    Qt::DockWidgetArea area = areaHint;
    if (area == Qt::NoDockWidgetArea)
        area = inferRelativeArea(a, b);
    if (area == Qt::NoDockWidgetArea)
        area = Qt::BottomDockWidgetArea;
    m_coreDockDirectionalAreas.insert(aToB, area);
    m_coreDockDirectionalAreas.insert(bToA, oppositeArea(area));
}

void MainWindow::toggleCoreDockConnectionByKey(const QString &pairKey,
                                                Qt::DockWidgetArea areaHint)
{
    if (pairKey.isEmpty())
        return;

    const QStringList names = pairKey.split(QLatin1Char('|'));
    if (names.size() != 2)
        return;

    auto findCoreDock = [this](const QString &name) -> DockWidget * {
        for (DockWidget *dock : coreGroupableDocks()) {
            if (dock && dock->objectName() == name)
                return dock;
        }
        return nullptr;
    };

    DockWidget *first = findCoreDock(names.at(0));
    DockWidget *second = findCoreDock(names.at(1));
    if (!first || !second)
        return;

    if (isCoreDockPairConnected(first, second)) {
        setCoreDockPairConnected(first, second, false);
        showStatusMsg(tr("Disconnected %1 and %2").arg(first->windowTitle(), second->windowTitle()));
    } else {
        if (first->isFloating() != second->isFloating()) {
            showStatusMsg(tr("Both docks must be either docked or floating"));
            scheduleCoreDockConnectOverlayRefresh();
            return;
        }

        Qt::DockWidgetArea area = areaHint;
        if (area == Qt::NoDockWidgetArea)
            area = inferRelativeArea(first, second);
        if (area == Qt::NoDockWidgetArea) {
            showStatusMsg(tr("Move the docks edge-to-edge before connecting"));
            scheduleCoreDockConnectOverlayRefresh();
            return;
        }
        setCoreDockPairConnected(first, second, true, area);
        // Apply relationship immediately so connected docks behave as one unit on drag.
        applyConnectedCoreDocks(first, false);
        showStatusMsg(tr("Connected %1 and %2").arg(first->windowTitle(), second->windowTitle()));
    }

    scheduleCoreDockConnectOverlayRefresh();
    scheduleLayoutHistoryCapture();
}

void MainWindow::applyConnectedCoreDocks(DockWidget *sourceDock, bool syncSize)
{
    if (m_syncingCoreDockConnections || m_connectedCoreDockPairs.isEmpty() || !content_window)
        return;

    const QList<DockWidget *> groupable = coreGroupableDocks();
    auto findDockByName = [groupable](const QString &name) -> DockWidget * {
        for (DockWidget *dock : groupable) {
            if (dock && dock->objectName() == name)
                return dock;
        }
        return nullptr;
    };

    auto applyFromTo = [syncSize, this](DockWidget *from, DockWidget *to, Qt::DockWidgetArea area) {
        if (!from || !to || from == to || !from->isVisible() || !to->isVisible())
            return;
        if (area == Qt::NoDockWidgetArea)
            return;

        auto placeRelative = [syncSize](DockWidget *leader,
                                        DockWidget *follower,
                                        Qt::DockWidgetArea relArea) {
            if (!leader || !follower || relArea == Qt::NoDockWidgetArea)
                return;

            const QRect src = leader->geometry();
            if (!src.isValid())
                return;

            QRect dst = follower->geometry();
            if (!dst.isValid()) {
                QSize fallback = follower->size();
                if ((!fallback.isValid() || fallback.width() <= 0 || fallback.height() <= 0) &&
                    follower->widget()) {
                    fallback = follower->widget()->sizeHint();
                }
                if (!fallback.isValid())
                    fallback = QSize(220, 120);
                dst = QRect(src.topLeft(), fallback);
            }

            switch (relArea) {
            case Qt::RightDockWidgetArea:
                dst.moveTopLeft(QPoint(src.right() + 1, src.top()));
                break;
            case Qt::LeftDockWidgetArea:
                dst.moveTopLeft(QPoint(src.left() - dst.width(), src.top()));
                break;
            case Qt::BottomDockWidgetArea:
                dst.moveTopLeft(QPoint(src.left(), src.bottom() + 1));
                break;
            case Qt::TopDockWidgetArea:
                dst.moveTopLeft(QPoint(src.left(), src.top() - dst.height()));
                break;
            default:
                return;
            }

            if (syncSize) {
                if (relArea == Qt::TopDockWidgetArea || relArea == Qt::BottomDockWidgetArea)
                    dst.setWidth(src.width());
                else if (relArea == Qt::LeftDockWidgetArea || relArea == Qt::RightDockWidgetArea)
                    dst.setHeight(src.height());
            }
            follower->setGeometry(dst);
        };

        if (from->isFloating() && to->isFloating()) {
            placeRelative(from, to, area);
            return;
        }

        if (from->isFloating() != to->isFloating()) {
            DockWidget *leader = from->isFloating() ? from : to;
            DockWidget *follower = from->isFloating() ? to : from;
            Qt::DockWidgetArea relArea = from->isFloating() ? area : this->oppositeArea(area);
            if (relArea == Qt::NoDockWidgetArea)
                return;

            QSize followerSize = follower->size();
            if ((!followerSize.isValid() || followerSize.width() <= 0 || followerSize.height() <= 0) &&
                follower->widget()) {
                followerSize = follower->widget()->sizeHint();
            }

            follower->setFloating(true);
            follower->setVisible(true);
            if (followerSize.isValid()) {
                QRect current = follower->geometry();
                if (current.isValid()) {
                    current.setSize(followerSize);
                    follower->setGeometry(current);
                } else {
                    follower->resize(followerSize);
                }
            }

            placeRelative(leader, follower, relArea);
            return;
        }

        // Avoid repeated dock reattachment loops while user drags/resizes docked panels.
        if (!from->isFloating() && !to->isFloating()) {
            addDockWidgetCompat(content_window, to, area, from, false, true);
            to->setVisible(true);
            return;
        }
    };

    m_syncingCoreDockConnections = true;
    if (sourceDock && !groupable.contains(sourceDock))
        sourceDock = nullptr;

    if (!sourceDock) {
        for (const QString &pairKey : std::as_const(m_connectedCoreDockPairs)) {
            const QStringList names = pairKey.split(QLatin1Char('|'));
            if (names.size() != 2)
                continue;
            DockWidget *a = findDockByName(names.at(0));
            DockWidget *b = findDockByName(names.at(1));
            if (!a || !b)
                continue;
            Qt::DockWidgetArea area = m_coreDockDirectionalAreas.value(
                makeCoreDirectionalKey(a->objectName(), b->objectName()),
                inferRelativeArea(a, b));
            if (area == Qt::NoDockWidgetArea)
                area = Qt::BottomDockWidgetArea;
            applyFromTo(a, b, area);
        }
    } else {
        QSet<QString> seen;
        QList<DockWidget *> queue{sourceDock};
        seen.insert(sourceDock->objectName());
        while (!queue.isEmpty()) {
            DockWidget *from = queue.takeFirst();
            if (!from)
                continue;
            const QString fromName = from->objectName();
            for (const QString &pairKey : std::as_const(m_connectedCoreDockPairs)) {
                const QStringList names = pairKey.split(QLatin1Char('|'));
                if (names.size() != 2)
                    continue;
                QString otherName;
                if (names.at(0) == fromName)
                    otherName = names.at(1);
                else if (names.at(1) == fromName)
                    otherName = names.at(0);
                else
                    continue;

                DockWidget *to = findDockByName(otherName);
                if (!to)
                    continue;
                Qt::DockWidgetArea area = m_coreDockDirectionalAreas.value(
                    makeCoreDirectionalKey(fromName, otherName),
                    inferRelativeArea(from, to));
                if (area == Qt::NoDockWidgetArea) {
                    area = inferRelativeArea(from, to);
                    if (area == Qt::NoDockWidgetArea)
                        continue;
                    m_coreDockDirectionalAreas.insert(makeCoreDirectionalKey(fromName, otherName), area);
                    m_coreDockDirectionalAreas.insert(makeCoreDirectionalKey(otherName, fromName), oppositeArea(area));
                }

                applyFromTo(from, to, area);
                if (!seen.contains(otherName)) {
                    seen.insert(otherName);
                    queue.append(to);
                }
            }
        }
    }
    m_syncingCoreDockConnections = false;
    scheduleCoreDockConnectOverlayRefresh();
}

void MainWindow::refreshCoreDockConnectOverlay()
{
    if (!content_window)
        return;

    const QList<DockWidget *> docks = coreGroupableDocks();
    QSet<QString> activeKeys;

    for (int i = 0; i < docks.size(); ++i) {
        DockWidget *a = docks.at(i);
        if (!a)
            continue;
        for (int j = i + 1; j < docks.size(); ++j) {
            DockWidget *b = docks.at(j);
            if (!b)
                continue;

            QPoint borderCenter;
            const Qt::DockWidgetArea area = inferRelativeArea(a, b, &borderCenter);
            if (area == Qt::NoDockWidgetArea)
                continue;
            Q_UNUSED(area);

            const QString pairKey = makeCorePairKey(a->objectName(), b->objectName());
            if (pairKey.isEmpty())
                continue;
            activeKeys.insert(pairKey);

            Qt::DockWidgetArea canonicalArea = area;
            if (a->objectName() > b->objectName())
                canonicalArea = oppositeArea(area);

            QToolButton *button = m_coreDockOverlayButtons.value(pairKey);
            if (!button) {
                button = new QToolButton(content_window);
                button->setCheckable(true);
                button->setAutoRaise(true);
                button->setFocusPolicy(Qt::NoFocus);
                button->setCursor(Qt::PointingHandCursor);
                button->setFixedSize(18, 18);
                button->setIconSize(QSize(12, 12));
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                connect(button, &QToolButton::clicked, this, [this, pairKey, button]() {
                    const Qt::DockWidgetArea areaHint =
                        static_cast<Qt::DockWidgetArea>(button->property("coreAreaHint").toInt());
                    toggleCoreDockConnectionByKey(pairKey, areaHint);
                });
                m_coreDockOverlayButtons.insert(pairKey, button);
            }
            button->setProperty("coreAreaHint", static_cast<int>(canonicalArea));

            const bool connected = m_connectedCoreDockPairs.contains(pairKey);
            button->setChecked(connected);
            const QColor iconColor = connected ? QColor(0x6C, 0xD7, 0x8D)
                                               : QColor(0xB4, 0xBC, 0xC8);
            button->setIcon(MaterialIcons::fromCodepoint(
                material_icon_font,
                connected ? MaterialIcons::CP::Link : MaterialIcons::CP::LinkOff,
                14,
                iconColor));
            button->setToolTip(connected ? tr("Disconnect these docks") : tr("Connect these docks"));

            DockWidget *anchorDock = b;
            if (area == Qt::TopDockWidgetArea || area == Qt::LeftDockWidgetArea)
                anchorDock = a;
            if (!anchorDock || !anchorDock->isVisible())
                anchorDock = a ? a : b;

            QWidget *titleBarHost = nullptr;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
            titleBarHost = kddTitleBarWidgetForDock(anchorDock);
#endif
            QWidget *overlayParent = titleBarHost ? titleBarHost
                                                  : (anchorDock ? static_cast<QWidget *>(anchorDock)
                                                                : static_cast<QWidget *>(content_window));
            if (overlayParent && button->parentWidget() != overlayParent)
                button->setParent(overlayParent);

            QPoint iconCenter;
            if (overlayParent) {
                iconCenter = QPoint(overlayParent->width() / 2, overlayParent->height() / 2);
                const QSize size = button->size();
                QPoint topLeft = iconCenter - QPoint(size.width() / 2, size.height() / 2);
                topLeft.setX(qBound(0, topLeft.x(), qMax(0, overlayParent->width() - size.width())));
                if (titleBarHost) {
                    const int maxY = qMax(0, overlayParent->height() - size.height());
                    const int desiredY = (overlayParent->height() - size.height()) / 2 - 1;
                    topLeft.setY(qBound(0, desiredY, maxY));
                } else {
                    topLeft.setY(0);
                }
                button->setGeometry(QRect(topLeft, size));
            } else {
                iconCenter = borderCenter;
                const QSize size = button->size();
                const QRect bounds = content_window->rect();
                QPoint topLeft = iconCenter - QPoint(size.width() / 2, size.height() / 2);
                topLeft.setX(qBound(0, topLeft.x(), qMax(0, bounds.width() - size.width())));
                topLeft.setY(qBound(0, topLeft.y(), qMax(0, bounds.height() - size.height())));
                button->setGeometry(QRect(topLeft, size));
            }
            button->show();
            button->raise();
        }
    }

    for (auto it = m_coreDockOverlayButtons.begin(); it != m_coreDockOverlayButtons.end(); ++it) {
        if (!it.value())
            continue;
        if (!activeKeys.contains(it.key()))
            it.value()->hide();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    DockWidget *dock = qobject_cast<DockWidget *>(watched);
    if (!dock)
        dock = m_coreDockWatchTargets.value(watched);
    if (dock && coreGroupableDocks().contains(dock) && event) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize: {
            if (dock->isFloating() || watched != dock) {
                QPointer<DockWidget> dockGuard(dock);
                const bool syncSizeNow = (event->type() == QEvent::Resize);
                QTimer::singleShot(0, this, [this, dockGuard, syncSizeNow]() {
                    if (!dockGuard)
                        return;
                    applyConnectedCoreDocks(dockGuard, syncSizeNow);
                });
            }
            refreshCoreDockConnectOverlay();
            scheduleCoreDockConnectOverlayRefresh();
            break;
        }
        case QEvent::Show:
        case QEvent::Hide:
            refreshCoreDockConnectOverlay();
            scheduleCoreDockConnectOverlayRefresh();
            break;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
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
    refreshCoreDockConnectOverlay();
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
