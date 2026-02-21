#include "mainwindow.h"

#include <QJsonArray>
#include <QJsonObject>

#include <climits>
#include <utility>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/KDDockWidgets.h>
#endif

#include "ui/dockwidget.h"

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
            // Re-dock relative placement using existing create/add helper path in docks.cpp.
            auto addDockWidgetCompat = [this](DockWidget *dock,
                                              Qt::DockWidgetArea dockArea,
                                              DockWidget *relativeTo) {
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
                if (auto *kdd = dynamic_cast<KDDockWidgets::QtWidgets::MainWindow *>(content_window)) {
                    KDDockWidgets::InitialOption initial;
                    const QSize current = dock->size();
                    if (current.isValid() && current.width() > 0 && current.height() > 0)
                        initial.preferredSize = current;
                    kdd->addDockWidget(
                        dock,
                        dockArea == Qt::LeftDockWidgetArea   ? KDDockWidgets::Location_OnLeft :
                        dockArea == Qt::TopDockWidgetArea    ? KDDockWidgets::Location_OnTop :
                        dockArea == Qt::BottomDockWidgetArea ? KDDockWidgets::Location_OnBottom :
                                                               KDDockWidgets::Location_OnRight,
                        relativeTo,
                        initial);
                    return;
                }
#else
                content_window->addDockWidget(dockArea, dock);
#endif
            };
            addDockWidgetCompat(to, area, from);
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
