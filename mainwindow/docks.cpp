#include "mainwindow.h"

#include <QEvent>
#include <QJsonArray>
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
