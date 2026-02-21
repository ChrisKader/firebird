#include "cyclecounterwidget.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QFont>
#include <QFontDatabase>

extern "C" {
#include "core/emu.h"
#include "core/timing/schedule.h"
}

CycleCounterWidget::CycleCounterWidget(QWidget *parent)
    : QWidget(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *form = new QFormLayout;
    form->setSpacing(4);

    m_totalLabel = new QLabel(QStringLiteral("0"), this);
    m_totalLabel->setFont(mono);
    m_totalLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Total cycles:"), m_totalLabel);

    m_deltaLabel = new QLabel(QStringLiteral("0"), this);
    m_deltaLabel->setFont(mono);
    m_deltaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Since reset:"), m_deltaLabel);

    m_timeLabel = new QLabel(QStringLiteral("0 us"), this);
    m_timeLabel->setFont(mono);
    m_timeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Est. time:"), m_timeLabel);

    m_clockLabel = new QLabel(this);
    m_clockLabel->setFont(mono);
    form->addRow(tr("CPU clock:"), m_clockLabel);

    layout->addLayout(form);

    m_resetBtn = new QPushButton(tr("Reset Counter"), this);
    connect(m_resetBtn, &QPushButton::clicked, this, &CycleCounterWidget::resetCounter);
    layout->addWidget(m_resetBtn);

    layout->addStretch();
}

void CycleCounterWidget::refresh()
{
    /* cycle_count_delta counts *down* from a positive reset value.
     * The total executed cycles are approximated by reading it.
     * sched.clock_rates[CLOCK_CPU] gives the CPU frequency. */
    int64_t total = -static_cast<int64_t>(cycle_count_delta);
    int64_t delta = total - m_baselineCycles;
    if (delta < 0) delta = 0;

    m_totalLabel->setText(QString::number(total));
    m_deltaLabel->setText(QString::number(delta));

    uint32_t cpuClock = sched.clock_rates[CLOCK_CPU];
    m_clockLabel->setText(QStringLiteral("%1 MHz").arg(cpuClock / 1000000.0, 0, 'f', 1));

    if (cpuClock > 0) {
        double seconds = static_cast<double>(delta) / cpuClock;
        if (seconds < 0.001)
            m_timeLabel->setText(QStringLiteral("%1 us").arg(seconds * 1e6, 0, 'f', 1));
        else if (seconds < 1.0)
            m_timeLabel->setText(QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'f', 3));
        else
            m_timeLabel->setText(QStringLiteral("%1 s").arg(seconds, 0, 'f', 6));
    } else {
        m_timeLabel->setText(tr("N/A"));
    }
}

void CycleCounterWidget::resetCounter()
{
    m_baselineCycles = -static_cast<int64_t>(cycle_count_delta);
    refresh();
}
