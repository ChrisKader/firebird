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
    custom_titlebar->setFixedHeight(24);

    QHBoxLayout *layout = new QHBoxLayout(custom_titlebar);
    layout->setContentsMargins(6, 1, 6, 1);
    layout->setSpacing(4);

    title_label = new QLabel(windowTitle(), custom_titlebar);
    title_label->setObjectName(QStringLiteral("dockTitleLabel"));
    title_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    layout->addWidget(title_label);
    layout->addStretch(1);

    float_button = new QToolButton(custom_titlebar);
    float_button->setObjectName(QStringLiteral("dockFloatButton"));
    float_button->setAutoRaise(false);
    float_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    float_button->setIconSize(QSize(18, 18));
    layout->addWidget(float_button);
    layout->setAlignment(float_button, Qt::AlignVCenter);
    connect(float_button, &QToolButton::clicked, this, [this]() { setFloating(!isFloating()); });

    close_button = new QToolButton(custom_titlebar);
    close_button->setObjectName(QStringLiteral("dockCloseButton"));
    close_button->setAutoRaise(false);
    close_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close_button->setIconSize(QSize(18, 18));
    layout->addWidget(close_button);
    layout->setAlignment(close_button, Qt::AlignVCenter);
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
        return;
    }

    if(hide_titlebar_if_possible)
    {
        custom_titlebar->setContentsMargins(6, 0, 6, 0);
        custom_titlebar->setVisible(true);
    }
    else
    {
        custom_titlebar->setContentsMargins(6, 1, 6, 1);
        custom_titlebar->setVisible(true);
    }
}

void DockWidget::updateCustomTitle(const QString &title)
{
    if(title_label)
        title_label->setText(title);
}

void DockWidget::applyButtonStyle(const QFont &iconFont)
{
    const QSize closeSize(24, 20);
    const QSize floatSize(28, 20);

    if (float_button)
    {
        float_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        float_button->setFixedSize(floatSize);
    }
    if (close_button)
    {
        close_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        close_button->setFixedSize(closeSize);
    }

    if (!iconFont.family().isEmpty())
    {
        QFont f = iconFont;
        f.setPixelSize(16);
        if (float_button)
        {
            float_button->setIcon(QIcon());
            float_button->setFont(f);
            float_button->setText(QString(QChar(0xF1CE))); // open_in_full
            float_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        }
        if (close_button)
        {
            close_button->setIcon(QIcon());
            close_button->setFont(f);
            close_button->setText(QString(QChar(0xE5CD))); // close
            close_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        }
    }
    else
    {
        // Fallback to standard icons if no custom font is available
        if (float_button)
        {
            float_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
            float_button->setIconSize(QSize(16, 16));
        }
        if (close_button)
        {
            close_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
            close_button->setIconSize(QSize(16, 16));
        }
    }
}
