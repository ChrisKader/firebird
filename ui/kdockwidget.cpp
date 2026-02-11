#include "ui/kdockwidget.h"

#include <QAction>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS

KDockWidget::KDockWidget(const QString &uniqueName,
                         const QString &dockTitle,
                         QWidget *parent)
    : KDDockWidgets::QtWidgets::DockWidget(uniqueName)
{
    Q_UNUSED(parent);
    setObjectName(uniqueName);
    setTitle(dockTitle);
    setWindowTitle(dockTitle);

    connect(this, &KDDockWidgets::QtWidgets::DockWidget::windowTitleChanged,
            this, [this](const QString &text) {
                if (text != this->title())
                    setTitle(text);
            });
    connect(this, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged,
            this, [this](bool floating) {
                emit topLevelChanged(floating);
                emit dockLocationChanged(Qt::NoDockWidgetArea);
            });
    connect(this, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged,
            this, [this](bool visible) {
                emit visibilityChanged(visible);
                emit dockLocationChanged(Qt::NoDockWidgetArea);
            });

    syncCompatibilityOptions();
}

void KDockWidget::applyThinTitlebar(bool enabled)
{
    Q_UNUSED(enabled);
}

void KDockWidget::setDockIcon(const QIcon &icon)
{
    setIcon(icon);
    QAction *toggle = toggleViewAction();
    if (toggle)
        toggle->setIcon(icon);
}

void KDockWidget::hideTitlebar(bool)
{
}

void KDockWidget::applyThinBarStyle()
{
}

void KDockWidget::refreshTitlebar()
{
}

void KDockWidget::setAllowedAreas(Qt::DockWidgetAreas areas)
{
    m_allowedAreas = areas;
    syncCompatibilityOptions();
}

void KDockWidget::setFeatures(QDockWidget::DockWidgetFeatures features)
{
    m_features = features;
    syncCompatibilityOptions();
}

void KDockWidget::syncCompatibilityOptions()
{
    KDDockWidgets::DockWidgetOptions options = this->options();

    if ((m_features & QDockWidget::DockWidgetClosable) == 0)
        options |= KDDockWidgets::DockWidgetOption_NotClosable;
    else
        options &= ~KDDockWidgets::DockWidgetOption_NotClosable;

    setOptions(options);

    QAction *floatToggle = floatAction();
    if (floatToggle)
        floatToggle->setEnabled((m_features & QDockWidget::DockWidgetFloatable) != 0);
}

#else

KDockWidget::KDockWidget(const QString &uniqueName,
                         const QString &dockTitle,
                         QWidget *parent)
    : DockWidget(uniqueName, parent)
{
    setObjectName(uniqueName);
    setWindowTitle(dockTitle);
}

void KDockWidget::applyThinTitlebar(bool enabled)
{
    hideTitlebar(enabled);
}

void KDockWidget::setDockIcon(const QIcon &icon)
{
    QAction *toggle = toggleViewAction();
    if (toggle)
        toggle->setIcon(icon);
}

#endif
