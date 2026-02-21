#include "mainwindow.h"

#include <QEvent>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/qtcommon/View.h>
    #include <kddockwidgets/core/TitleBar.h>
    #include <kddockwidgets/core/View.h>
#endif

#include "ui/dockwidget.h"
#include "ui/kdockwidget.h"
#include "ui/materialicons.h"

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
static QWidget *kddTitleBarWidgetForDock(DockWidget *dock)
{
    if (!dock)
        return nullptr;

    if (auto *kDock = dynamic_cast<KDockWidget *>(dock)) {
        if (auto *titleBar = kDock->actualTitleBar()) {
            if (auto *view = titleBar->view())
                return KDDockWidgets::QtCommon::View_qt::asQWidget(view);
        }
    }

    return nullptr;
}
#endif

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
