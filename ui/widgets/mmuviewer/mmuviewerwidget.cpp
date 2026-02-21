#include "mmuviewerwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QGroupBox>
#include <QSplitter>

extern "C" {
#include "core/memory/mem.h"
#include "core/memory/mmu.h"
}

#include "core/debug/debug_api.h"
#include "ui/theme/widgettheme.h"

MMUViewerWidget::MMUViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    /* Auto-refresh toolbar */
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(tr("Refresh:"), this));
    m_refreshCombo = new QComboBox(this);
    m_refreshCombo->addItems({tr("Manual"), tr("500 ms"), tr("1 s"), tr("2 s")});
    topBar->addWidget(m_refreshCombo);
    topBar->addStretch();

    /* VA -> PA translation */
    topBar->addWidget(new QLabel(tr("VA:"), this));
    m_vaInput = new QLineEdit(this);
    m_vaInput->setPlaceholderText(QStringLiteral("e.g. 10000000"));
    m_vaInput->setMaximumWidth(120);
    m_vaInput->setFont(mono);
    topBar->addWidget(m_vaInput);

    auto *translateBtn = new QPushButton(tr("Translate"), this);
    topBar->addWidget(translateBtn);

    m_paOutput = new QLabel(this);
    m_paOutput->setFont(mono);
    m_paOutput->setMinimumWidth(180);
    topBar->addWidget(m_paOutput);

    layout->addLayout(topBar);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MMUViewerWidget::refresh);
    connect(m_refreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MMUViewerWidget::onAutoRefreshChanged);
    connect(translateBtn, &QPushButton::clicked, this, &MMUViewerWidget::onTranslate);
    connect(m_vaInput, &QLineEdit::returnPressed, this, &MMUViewerWidget::onTranslate);

    /* Splitter for the three trees */
    auto *splitter = new QSplitter(Qt::Vertical, this);

    /* CP15 registers */
    m_cp15Tree = new QTreeWidget(this);
    m_cp15Tree->setHeaderLabels({tr("Register"), tr("Value"), tr("Decoded")});
    m_cp15Tree->setRootIsDecorated(false);
    m_cp15Tree->setFont(mono);
    m_cp15Tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_cp15Tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_cp15Tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_cp15Tree->setMaximumHeight(160);
    splitter->addWidget(m_cp15Tree);

    /* L1 page table */
    m_l1Tree = new QTreeWidget(this);
    m_l1Tree->setHeaderLabels({tr("Index"), tr("VA Range"), tr("Type"),
                                tr("Domain"), tr("AP"), tr("Physical / L2 Ptr")});
    m_l1Tree->setRootIsDecorated(false);
    m_l1Tree->setFont(mono);
    m_l1Tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_l1Tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_l1Tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_l1Tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_l1Tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_l1Tree->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    connect(m_l1Tree, &QTreeWidget::itemClicked, this, &MMUViewerWidget::onL1ItemClicked);
    splitter->addWidget(m_l1Tree);

    /* L2 page table */
    m_l2Tree = new QTreeWidget(this);
    m_l2Tree->setHeaderLabels({tr("L2 Idx"), tr("VA"), tr("Type"),
                                tr("AP"), tr("Physical Addr")});
    m_l2Tree->setRootIsDecorated(false);
    m_l2Tree->setFont(mono);
    m_l2Tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_l2Tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_l2Tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_l2Tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_l2Tree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    splitter->addWidget(m_l2Tree);

    splitter->setStretchFactor(0, 0); /* CP15: fixed */
    splitter->setStretchFactor(1, 2); /* L1: larger */
    splitter->setStretchFactor(2, 1); /* L2: smaller */

    layout->addWidget(splitter);
}

void MMUViewerWidget::onAutoRefreshChanged(int index)
{
    m_refreshTimer->stop();
    switch (index) {
    case 1: m_refreshTimer->start(500); break;
    case 2: m_refreshTimer->start(1000); break;
    case 3: m_refreshTimer->start(2000); break;
    default: break;
    }
}

uint32_t MMUViewerWidget::readPhys32(uint32_t paddr)
{
    void *ptr = phys_mem_ptr(paddr, 4);
    if (!ptr)
        return 0xDEADBEEF;
    return *(uint32_t *)ptr;
}

QString MMUViewerWidget::decodeFaultStatus(uint32_t fsr)
{
    uint32_t type = fsr & 0xF;
    int domain = (fsr >> 4) & 0xF;
    QString typeName;
    switch (type) {
    case 0x0: typeName = QStringLiteral("None"); break;
    case 0x1: typeName = QStringLiteral("Alignment"); break;
    case 0x2: typeName = QStringLiteral("Terminal"); break;
    case 0x3: typeName = QStringLiteral("Alignment (3)"); break;
    case 0x4: typeName = QStringLiteral("Section linefetch"); break;
    case 0x5: typeName = QStringLiteral("Section translation"); break;
    case 0x6: typeName = QStringLiteral("Page linefetch"); break;
    case 0x7: typeName = QStringLiteral("Page translation"); break;
    case 0x8: typeName = QStringLiteral("Section access"); break;
    case 0x9: typeName = QStringLiteral("Section domain"); break;
    case 0xA: typeName = QStringLiteral("Page access"); break;
    case 0xB: typeName = QStringLiteral("Page domain"); break;
    case 0xC: typeName = QStringLiteral("L1 ext abort (xlat)"); break;
    case 0xD: typeName = QStringLiteral("Section permission"); break;
    case 0xE: typeName = QStringLiteral("L2 ext abort (xlat)"); break;
    case 0xF: typeName = QStringLiteral("Page permission"); break;
    }
    if (type == 0)
        return QStringLiteral("No fault");
    return QStringLiteral("D%1: %2").arg(domain).arg(typeName);
}

QString MMUViewerWidget::decodeAP(uint32_t ap)
{
    switch (ap & 3) {
    case 0: return QStringLiteral("No access");
    case 1: return QStringLiteral("SVC R/W");
    case 2: return QStringLiteral("SVC R/W, USR RO");
    case 3: return QStringLiteral("R/W");
    }
    return QStringLiteral("?");
}

QString MMUViewerWidget::decodeDomainAccess(uint32_t dacr, int domain)
{
    uint32_t mode = (dacr >> (domain * 2)) & 3;
    switch (mode) {
    case 0: return QStringLiteral("No Access");
    case 1: return QStringLiteral("Client");
    case 2: return QStringLiteral("Reserved");
    case 3: return QStringLiteral("Manager");
    }
    return QStringLiteral("?");
}

void MMUViewerWidget::refresh()
{
    if (!isVisible())
        return;

    populateCP15();
    populateL1Table();
}

void MMUViewerWidget::populateCP15()
{
    m_cp15Tree->clear();

    const WidgetTheme &theme = currentWidgetTheme();
    uint32_t cp15[6];
    debug_get_cp15(cp15);

    auto hex = [](uint32_t v) {
        return QStringLiteral("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };

    auto addRow = [&](const QString &name, uint32_t val, const QString &decoded) {
        auto *item = new QTreeWidgetItem(m_cp15Tree);
        item->setText(0, name);
        item->setText(1, hex(val));
        item->setText(2, decoded);
        item->setForeground(0, QBrush(theme.syntaxRegister));
        item->setForeground(1, QBrush(theme.syntaxImmediate));
        item->setForeground(2, QBrush(theme.syntaxSymbol));
    };

    /* SCTLR */
    {
        uint32_t v = cp15[0];
        QString decoded = QStringLiteral("MMU=%1 A=%2 C=%3 W=%4 I=%5")
            .arg((v & 1) ? QStringLiteral("ON") : QStringLiteral("off"))
            .arg((v >> 1) & 1).arg((v >> 2) & 1)
            .arg((v >> 3) & 1).arg((v >> 12) & 1);
        addRow(QStringLiteral("SCTLR"), v, decoded);
    }

    /* TTBR0 */
    {
        uint32_t v = cp15[1];
        uint32_t base = v & 0xFFFFC000;
        addRow(QStringLiteral("TTBR0"), v,
               QStringLiteral("L1 table at 0x%1").arg(base, 8, 16, QLatin1Char('0')));
    }

    /* DACR */
    {
        uint32_t v = cp15[2];
        QStringList parts;
        for (int d = 0; d < 16; d++) {
            uint32_t mode = (v >> (d * 2)) & 3;
            if (mode != 0)
                parts << QStringLiteral("D%1=%2").arg(d).arg(decodeDomainAccess(v, d));
        }
        addRow(QStringLiteral("DACR"), v,
               parts.isEmpty() ? QStringLiteral("All domains: No Access") : parts.join(QStringLiteral(", ")));
    }

    /* DFSR */
    addRow(QStringLiteral("DFSR"), cp15[3], decodeFaultStatus(cp15[3]));

    /* IFSR */
    addRow(QStringLiteral("IFSR"), cp15[4], decodeFaultStatus(cp15[4]));

    /* FAR */
    addRow(QStringLiteral("FAR"), cp15[5],
           cp15[5] ? QStringLiteral("Faulting VA") : QStringLiteral("No fault"));
}

void MMUViewerWidget::populateL1Table()
{
    m_l1Tree->clear();
    m_l2Tree->clear();

    const WidgetTheme &theme = currentWidgetTheme();

    uint32_t cp15[6];
    debug_get_cp15(cp15);

    /* Check if MMU is enabled */
    if (!(cp15[0] & 1)) {
        auto *item = new QTreeWidgetItem(m_l1Tree);
        item->setText(0, QStringLiteral("MMU disabled - flat mapping"));
        item->setForeground(0, QBrush(theme.textMuted));
        return;
    }

    uint32_t ttb = cp15[1] & 0xFFFFC000;

    /* Walk all 4096 L1 entries, only show valid ones */
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t desc = readPhys32(ttb + i * 4);
        uint32_t type = desc & 3;

        if (type == 0)
            continue; /* skip invalid/unmapped entries */

        uint32_t va_base = i << 20;
        auto *item = new QTreeWidgetItem(m_l1Tree);
        item->setText(0, QStringLiteral("%1").arg(i, 3, 16, QLatin1Char('0')));
        item->setText(1, QStringLiteral("%1-%2")
            .arg(va_base, 8, 16, QLatin1Char('0'))
            .arg(va_base + 0xFFFFF, 8, 16, QLatin1Char('0')));

        int domain = (desc >> 5) & 0xF;
        item->setText(3, QString::number(domain));

        /* Store data for L2 drill-down */
        item->setData(0, Qt::UserRole, desc);
        item->setData(0, Qt::UserRole + 1, va_base);

        switch (type) {
        case 1: { /* Coarse page table */
            uint32_t l2_base = desc & 0xFFFFFC00;
            item->setText(2, QStringLiteral("Coarse"));
            item->setText(4, QStringLiteral("-"));
            item->setText(5, QStringLiteral("L2 @ 0x%1").arg(l2_base, 8, 16, QLatin1Char('0')));
            item->setForeground(2, QBrush(theme.syntaxBranch));
            break;
        }
        case 2: { /* Section (1MB) */
            uint32_t pa = desc & 0xFFF00000;
            uint32_t ap = (desc >> 10) & 3;
            item->setText(2, QStringLiteral("Section"));
            item->setText(4, decodeAP(ap));
            item->setText(5, QStringLiteral("0x%1").arg(pa, 8, 16, QLatin1Char('0')));
            item->setForeground(2, QBrush(theme.syntaxMnemonic));
            break;
        }
        case 3: { /* Fine page table */
            uint32_t l2_base = desc & 0xFFFFF000;
            item->setText(2, QStringLiteral("Fine"));
            item->setText(4, QStringLiteral("-"));
            item->setText(5, QStringLiteral("L2 @ 0x%1").arg(l2_base, 8, 16, QLatin1Char('0')));
            item->setForeground(2, QBrush(theme.syntaxBranch));
            break;
        }
        }

        item->setForeground(0, QBrush(theme.syntaxRegister));
        item->setForeground(1, QBrush(theme.syntaxAddress));
        item->setForeground(5, QBrush(theme.syntaxImmediate));
    }
}

void MMUViewerWidget::onL1ItemClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;

    uint32_t desc = item->data(0, Qt::UserRole).toUInt();
    uint32_t va_base = item->data(0, Qt::UserRole + 1).toUInt();
    uint32_t type = desc & 3;

    if (type == 1 || type == 3)
        populateL2Table(desc, va_base);
    else
        m_l2Tree->clear();
}

void MMUViewerWidget::populateL2Table(uint32_t l1_desc, uint32_t va_base)
{
    m_l2Tree->clear();

    const WidgetTheme &theme = currentWidgetTheme();
    uint32_t type = l1_desc & 3;

    uint32_t l2_base, num_entries;
    uint32_t va_shift;

    if (type == 1) {
        /* Coarse page table: 256 entries, each covers 4KB */
        l2_base = l1_desc & 0xFFFFFC00;
        num_entries = 256;
        va_shift = 12;
    } else {
        /* Fine page table: 1024 entries, each covers 1KB */
        l2_base = l1_desc & 0xFFFFF000;
        num_entries = 1024;
        va_shift = 10;
    }

    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t desc = readPhys32(l2_base + i * 4);
        uint32_t l2_type = desc & 3;

        if (l2_type == 0)
            continue; /* skip invalid */

        uint32_t va = va_base + (i << va_shift);
        auto *item = new QTreeWidgetItem(m_l2Tree);
        item->setText(0, QStringLiteral("%1").arg(i, 3, 16, QLatin1Char('0')));
        item->setText(1, QStringLiteral("%1").arg(va, 8, 16, QLatin1Char('0')));

        uint32_t ap = (desc >> 4) & 3;

        switch (l2_type) {
        case 1: { /* Large page (64KB) */
            uint32_t pa = desc & 0xFFFF0000;
            item->setText(2, QStringLiteral("Large 64K"));
            item->setText(3, decodeAP(ap));
            item->setText(4, QStringLiteral("0x%1").arg(pa | (va & 0xFFFF), 8, 16, QLatin1Char('0')));
            break;
        }
        case 2: { /* Small page (4KB) */
            uint32_t pa = desc & 0xFFFFF000;
            item->setText(2, QStringLiteral("Small 4K"));
            item->setText(3, decodeAP(ap));
            item->setText(4, QStringLiteral("0x%1").arg(pa | (va & 0xFFF), 8, 16, QLatin1Char('0')));
            break;
        }
        case 3: { /* Tiny page (1KB) */
            uint32_t pa = desc & 0xFFFFFC00;
            item->setText(2, QStringLiteral("Tiny 1K"));
            item->setText(3, decodeAP(ap));
            item->setText(4, QStringLiteral("0x%1").arg(pa | (va & 0x3FF), 8, 16, QLatin1Char('0')));
            break;
        }
        }

        item->setForeground(0, QBrush(theme.syntaxRegister));
        item->setForeground(1, QBrush(theme.syntaxAddress));
        item->setForeground(4, QBrush(theme.syntaxImmediate));
    }
}

void MMUViewerWidget::onTranslate()
{
    QString text = m_vaInput->text().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        text = text.mid(2);

    bool ok = false;
    uint32_t va = text.toUInt(&ok, 16);
    if (!ok || text.isEmpty()) {
        m_paOutput->setText(QStringLiteral("Invalid address"));
        return;
    }

    uint32_t cp15[6];
    debug_get_cp15(cp15);

    if (!(cp15[0] & 1)) {
        /* MMU off: VA == PA */
        m_paOutput->setText(QStringLiteral("PA: 0x%1 (MMU off)")
            .arg(va, 8, 16, QLatin1Char('0')));
        return;
    }

    uint32_t pa = mmu_translate(va, false, NULL, NULL);
    if (pa == 0xFFFFFFFF) {
        m_paOutput->setText(QStringLiteral("Translation fault"));
        m_paOutput->setStyleSheet(QStringLiteral("color: #F44336;"));
    } else {
        m_paOutput->setText(QStringLiteral("PA: 0x%1").arg(pa, 8, 16, QLatin1Char('0')));
        m_paOutput->setStyleSheet(QStringLiteral("color: #4CAF50;"));
    }
}
