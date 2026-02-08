#include "stackwidget.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QFontDatabase>

#include "core/debug_api.h"

StackWidget::StackWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Address"), tr("Value"), tr("Info")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_tree->setFont(mono);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item, int) {
        if (item) {
            uint32_t addr = item->data(1, Qt::UserRole).toUInt();
            emit goToAddress(addr);
        }
    });

    layout->addWidget(m_tree);
}

void StackWidget::refresh()
{
    m_tree->clear();

    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;
    debug_get_registers(regs, &cpsr, &spsr, &has_spsr);

    uint32_t sp = regs[13];
    uint32_t lr = regs[14];
    uint32_t pc = regs[15];

    /* Show stack contents from SP upward */
    for (int i = 0; i < MAX_STACK_WORDS; i++) {
        uint32_t addr = sp + (uint32_t)(i * 4);
        uint32_t val = 0;
        if (debug_read_memory(addr, &val, 4) != 4)
            break;

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
        item->setText(1, QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0')));
        item->setData(1, Qt::UserRole, val);

        /* Annotate special values */
        if (addr == sp)
            item->setText(2, QStringLiteral("SP ->"));
        if (val == lr)
            item->setText(2, item->text(2) + QStringLiteral(" [LR]"));
        if (val == pc)
            item->setText(2, item->text(2) + QStringLiteral(" [PC]"));

        /* Heuristic: values in code range might be return addresses */
        if (val >= 0x10000000 && val < 0x12000000 && val != lr && val != pc)
            item->setText(2, item->text(2) + QStringLiteral(" ret?"));
    }
}
