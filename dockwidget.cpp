#include <QHBoxLayout>
#include <QSizePolicy>
#include <QStyle>

#include "dockwidget.h"

DockWidget::DockWidget(const QString &title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)
{
    buildCustomTitlebar();
    setTitleBarWidget(custom_titlebar);

    connect(this, SIGNAL(topLevelChanged(bool)), this, SLOT(refreshTitlebar()));
    connect(this, &QDockWidget::windowTitleChanged, this, &DockWidget::updateCustomTitle);
}

void DockWidget::hideTitlebar(bool b)
{
    hide_titlebar_if_possible = b;
    refreshTitlebar();
}

void DockWidget::buildCustomTitlebar()
{
    custom_titlebar = new QWidget(this);
    custom_titlebar->setObjectName(QStringLiteral("dockTitleBar"));
    custom_titlebar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout *layout = new QHBoxLayout(custom_titlebar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    title_label = new QLabel(windowTitle(), custom_titlebar);
    title_label->setObjectName(QStringLiteral("dockTitleLabel"));
    layout->addWidget(title_label);
    layout->addStretch(1);

    float_button = new QToolButton(custom_titlebar);
    float_button->setObjectName(QStringLiteral("dockFloatButton"));
    float_button->setAutoRaise(true);
    float_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    float_button->setIconSize(QSize(14, 14));
    layout->addWidget(float_button);
    connect(float_button, &QToolButton::clicked, this, [this]() { setFloating(!isFloating()); });

    close_button = new QToolButton(custom_titlebar);
    close_button->setObjectName(QStringLiteral("dockCloseButton"));
    close_button->setAutoRaise(true);
    close_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close_button->setIconSize(QSize(14, 14));
    layout->addWidget(close_button);
    connect(close_button, &QToolButton::clicked, this, &DockWidget::close);
}

void DockWidget::refreshTitlebar()
{
    if(!custom_titlebar)
        return;

    // Re-apply in case Qt recreated the internal frame during a drag/float operation.
    if(titleBarWidget() != custom_titlebar)
        setTitleBarWidget(custom_titlebar);

    // Always show a styled titlebar while floating so the dock stays draggable and closable.
    if(isFloating())
    {
        custom_titlebar->setVisible(true);
        custom_titlebar->setMaximumHeight(QWIDGETSIZE_MAX);
        custom_titlebar->setMinimumHeight(0);
        return;
    }

    if(hide_titlebar_if_possible)
    {
        custom_titlebar->setContentsMargins(6, 2, 6, 2);
        custom_titlebar->setMaximumHeight(12);
        custom_titlebar->setMinimumHeight(8);
        custom_titlebar->setVisible(true);
    }
    else
    {
        custom_titlebar->setContentsMargins(8, 4, 8, 4);
        custom_titlebar->setMaximumHeight(QWIDGETSIZE_MAX);
        custom_titlebar->setMinimumHeight(0);
        custom_titlebar->setVisible(true);
    }
}

void DockWidget::updateCustomTitle(const QString &title)
{
    if(title_label)
        title_label->setText(title);
}
