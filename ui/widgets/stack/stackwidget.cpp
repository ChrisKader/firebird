#include "stackwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QFontDatabase>
#include <QMenu>
#include <QClipboard>
#include <QApplication>

#include "core/debug_api.h"
#include "ui/theme/widgettheme.h"

StackWidget::StackWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    /* SP label and depth control */
    auto *topLayout = new QHBoxLayout;
    topLayout->setContentsMargins(4, 2, 4, 2);

    m_spLabel = new QLabel(this);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_spLabel->setFont(mono);
    topLayout->addWidget(m_spLabel);

    topLayout->addStretch();

    auto *depthLabel = new QLabel(tr("Depth:"), this);
    topLayout->addWidget(depthLabel);

    m_depthSpin = new QSpinBox(this);
    m_depthSpin->setRange(16, 256);
    m_depthSpin->setValue(DEFAULT_STACK_WORDS);
    m_depthSpin->setSingleStep(16);
    connect(m_depthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refresh(); });
    topLayout->addWidget(m_depthSpin);

    layout->addLayout(topLayout);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Address"), tr("Value"), tr("Info")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    m_tree->setFont(mono);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item, int) {
        if (item) {
            uint32_t addr = item->data(1, Qt::UserRole).toUInt();
            emit goToAddress(addr);
        }
    });

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &StackWidget::contextMenuAt);

    layout->addWidget(m_tree);
}

bool StackWidget::looksLikeReturnAddr(uint32_t val) const
{
    /* Heuristic: values in typical Nspire code ranges could be return addresses.
     * OS code: 0x10000000-0x12000000, Boot ROM: 0x00000000-0x00040000 */
    return (val >= 0x10000000 && val < 0x12000000) ||
           (val >= 0x00000100 && val < 0x00040000);
}

void StackWidget::refresh()
{
    if (!isVisible())
        return;

    m_tree->clear();

    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;
    debug_get_registers(regs, &cpsr, &spsr, &has_spsr);

    uint32_t sp = regs[13];
    uint32_t lr = regs[14];
    uint32_t pc = regs[15];

    m_spLabel->setText(QStringLiteral("SP = %1").arg(sp, 8, 16, QLatin1Char('0')));

    int depth = m_depthSpin->value();
    bool lastWasFrame = false;

    for (int i = 0; i < depth; i++) {
        uint32_t addr = sp + (uint32_t)(i * 4);
        uint32_t val = 0;
        if (debug_read_memory(addr, &val, 4) != 4)
            break;

        /* Detect frame boundaries: value looks like a return address
         * and is preceded by what could be a saved frame pointer */
        bool isRetAddr = looksLikeReturnAddr(val);
        bool isFrameBoundary = isRetAddr && !lastWasFrame && i > 0;

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
        item->setText(1, QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0')));
        item->setData(0, Qt::UserRole, addr);
        item->setData(1, Qt::UserRole, val);

        /* Build info annotation */
        QStringList info;
        if (addr == sp)
            info << QStringLiteral("SP ->");
        if (val == lr)
            info << QStringLiteral("[LR]");
        if (val == pc)
            info << QStringLiteral("[PC]");

        /* Show symbol if available */
        if (m_symbols && isRetAddr) {
            /* Check nearby addresses for symbols (BL returns to addr after call) */
            for (int off = 0; off >= -4; off -= 2) {
                auto it = m_symbols->find(val + off);
                if (it != m_symbols->end()) {
                    info << QStringLiteral("<%1>").arg(it.value());
                    break;
                }
            }
        }

        if (isRetAddr && val != lr && val != pc)
            info << QStringLiteral("ret?");

        item->setText(2, info.join(QStringLiteral(" ")));

        /* Draw frame separator via background color on boundary items */
        if (isFrameBoundary) {
            QBrush bg(currentWidgetTheme().frameSeparator);
            for (int c = 0; c < 3; c++)
                item->setBackground(c, bg);
        }

        lastWasFrame = isRetAddr;
    }
}

void StackWidget::contextMenuAt(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item) return;

    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    uint32_t val = item->data(1, Qt::UserRole).toUInt();

    QMenu menu(this);

    menu.addAction(tr("Go to Address in Disassembly"), this, [this, val]() {
        emit gotoDisassembly(val);
    });

    menu.addAction(tr("Go to Address in Memory"), this, [this, val]() {
        emit goToAddress(val);
    });

    menu.addSeparator();

    menu.addAction(tr("Copy Address"), this, [addr]() {
        QApplication::clipboard()->setText(
            QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
    });

    menu.addAction(tr("Copy Value"), this, [val]() {
        QApplication::clipboard()->setText(
            QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0')));
    });

    menu.exec(m_tree->mapToGlobal(pos));
}
