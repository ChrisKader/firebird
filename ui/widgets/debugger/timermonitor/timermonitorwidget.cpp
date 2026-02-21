#include "timermonitorwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>

extern "C" {
#include "core/emu.h"
}

#include "core/debug_api.h"
#include "ui/widgettheme.h"

TimerMonitorWidget::TimerMonitorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    /* Auto-refresh toolbar */
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(tr("Refresh:"), this));
    m_refreshCombo = new QComboBox(this);
    m_refreshCombo->addItems({tr("Manual"), tr("100 ms"), tr("500 ms"), tr("1 s")});
    topBar->addWidget(m_refreshCombo);
    topBar->addStretch();
    layout->addLayout(topBar);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &TimerMonitorWidget::refresh);
    connect(m_refreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimerMonitorWidget::onAutoRefreshChanged);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Timer"), tr("Value"), tr("Load"), tr("Control"),
                              tr("Prescale"), tr("Enabled"), tr("IRQ")});
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 7; i++)
        m_tree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_tree->setFont(mono);

    layout->addWidget(m_tree);
}

QJsonObject TimerMonitorWidget::serializeState() const
{
    QJsonObject state;
    if (m_refreshCombo)
        state.insert(QStringLiteral("refreshIndex"), m_refreshCombo->currentIndex());
    return state;
}

void TimerMonitorWidget::restoreState(const QJsonObject &state)
{
    if (!m_refreshCombo)
        return;
    const int idx = state.value(QStringLiteral("refreshIndex")).toInt(m_refreshCombo->currentIndex());
    if (idx >= 0 && idx < m_refreshCombo->count())
        m_refreshCombo->setCurrentIndex(idx);
}

void TimerMonitorWidget::onAutoRefreshChanged(int index)
{
    m_refreshTimer->stop();
    switch (index) {
    case 1: m_refreshTimer->start(100); break;
    case 2: m_refreshTimer->start(500); break;
    case 3: m_refreshTimer->start(1000); break;
    default: break;
    }
}

void TimerMonitorWidget::refresh()
{
    if (!isVisible())
        return;

    m_tree->clear();

    if (!emulate_cx) {
        addClassicTimers();
    } else {
        addCxTimers();
    }
    addWatchdog();

    m_tree->expandAll();

    /* Color items */
    const WidgetTheme &theme = currentWidgetTheme();
    std::function<void(QTreeWidgetItem *)> colorAll = [&](QTreeWidgetItem *item) {
        if (item->childCount() == 0) {
            item->setForeground(0, QBrush(theme.syntaxRegister));
            for (int c = 1; c < m_tree->columnCount(); c++)
                item->setForeground(c, QBrush(theme.syntaxImmediate));
            // Enabled/IRQ columns: use accent for Yes/Active
            for (int c : {5, 6}) {
                if (item->text(c) == QStringLiteral("Yes") || item->text(c) == QStringLiteral("Active"))
                    item->setForeground(c, QBrush(theme.accent));
            }
        } else {
            item->setForeground(0, QBrush(theme.syntaxMnemonic));
        }
        for (int i = 0; i < item->childCount(); i++)
            colorAll(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        colorAll(m_tree->topLevelItem(i));
}

void TimerMonitorWidget::addClassicTimers()
{
    auto hex = [](uint32_t v, int w = 8) {
        return QStringLiteral("%1").arg(v, w, 16, QLatin1Char('0'));
    };

    for (int p = 0; p < 3; p++) {
        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, QStringLiteral("Timer Pair %1").arg(p));

        /* Read timer pair state directly from structs (thread-safe) */
        for (int t = 0; t < 2; t++) {
            uint32_t base = 0x90010000 + p * 0x10000 + t * 0x20;
            uint32_t val = 0, load = 0, ctrl = 0;
            debug_peek_reg(base + 0x00, &val);
            debug_peek_reg(base + 0x04, &load);
            debug_peek_reg(base + 0x08, &ctrl);

            auto *item = new QTreeWidgetItem(group);
            item->setText(0, QStringLiteral("Timer %1.%2").arg(p).arg(t));
            item->setText(1, hex(val));
            item->setText(2, hex(load));
            item->setText(3, hex(ctrl, 4));
            item->setText(5, (ctrl & 0x80) ? QStringLiteral("Yes") : QStringLiteral("No"));
        }
    }
}

void TimerMonitorWidget::addCxTimers()
{
    auto hex = [](uint32_t v, int w = 8) {
        return QStringLiteral("%1").arg(v, w, 16, QLatin1Char('0'));
    };

    /* CX uses SP804-style timers at 0x90010000, 0x900C0000, 0x900D0000 */
    static const uint32_t bases[] = { 0x90010000, 0x900C0000, 0x900D0000 };
    static const char *names[] = { "Fast Timer", "Slow Timer 0", "Slow Timer 1" };

    for (int p = 0; p < 3; p++) {
        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, QString::fromLatin1(names[p]));

        for (int t = 0; t < 2; t++) {
            uint32_t base = bases[p] + t * 0x20;
            uint32_t load = 0, val = 0, ctrl = 0;
            debug_peek_reg(base + 0x00, &load);
            debug_peek_reg(base + 0x04, &val);
            debug_peek_reg(base + 0x08, &ctrl);

            uint32_t prescale = (ctrl >> 2) & 3;
            static const char *prescaleStr[] = { "1", "16", "256", "???" };

            auto *item = new QTreeWidgetItem(group);
            item->setText(0, QStringLiteral("%1.%2").arg(QString::fromLatin1(names[p])).arg(t));
            item->setText(1, hex(val));
            item->setText(2, hex(load));
            item->setText(3, hex(ctrl, 2));
            item->setText(4, QString::fromLatin1(prescaleStr[prescale]));
            item->setText(5, (ctrl & 0x80) ? QStringLiteral("Yes") : QStringLiteral("No"));
            item->setText(6, (ctrl & 0x20) ? QStringLiteral("Masked") : QStringLiteral("Active"));
        }
    }
}

void TimerMonitorWidget::addWatchdog()
{
    auto hex = [](uint32_t v, int w = 8) {
        return QStringLiteral("%1").arg(v, w, 16, QLatin1Char('0'));
    };

    auto *group = new QTreeWidgetItem(m_tree);
    group->setText(0, QStringLiteral("Watchdog"));

    uint32_t load = 0, val = 0, ctrl = 0;
    debug_peek_reg(0x90060000, &load);
    debug_peek_reg(0x90060004, &val);
    debug_peek_reg(0x90060008, &ctrl);

    auto *item = new QTreeWidgetItem(group);
    item->setText(0, QStringLiteral("Watchdog"));
    item->setText(1, hex(val));
    item->setText(2, hex(load));
    item->setText(3, hex(ctrl, 2));
    item->setText(5, (ctrl & 1) ? QStringLiteral("Yes") : QStringLiteral("No"));
}
