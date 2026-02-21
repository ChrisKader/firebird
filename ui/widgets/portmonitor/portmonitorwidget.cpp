#include "portmonitorwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QTextStream>
#include <QLabel>

#include "core/debug/debug_api.h"
#include "core/emu.h"
#include "ui/theme/widgettheme.h"

PortMonitorWidget::PortMonitorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    /* Toolbar */
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    QAction *addAct = m_toolbar->addAction(QStringLiteral("+"));
    addAct->setToolTip(tr("Add Port"));
    connect(addAct, &QAction::triggered, this, &PortMonitorWidget::addPort);

    QAction *removeAct = m_toolbar->addAction(QStringLiteral("\u2212"));
    removeAct->setToolTip(tr("Remove Port"));
    connect(removeAct, &QAction::triggered, this, &PortMonitorWidget::removePort);

    QAction *commonAct = m_toolbar->addAction(QStringLiteral("\u2261"));
    commonAct->setToolTip(tr("Add Common TI-Nspire MMIO Ports"));
    connect(commonAct, &QAction::triggered, this, &PortMonitorWidget::addCommonPorts);

    m_toolbar->addSeparator();

    QAction *exportAct = m_toolbar->addAction(QStringLiteral("CSV"));
    exportAct->setToolTip(tr("Export to CSV"));
    connect(exportAct, &QAction::triggered, this, &PortMonitorWidget::exportCSV);

    m_toolbar->addSeparator();

    auto *refreshLabel = new QLabel(tr("Refresh:"), this);
    m_toolbar->addWidget(refreshLabel);

    m_refreshCombo = new QComboBox(this);
    m_refreshCombo->addItems({tr("Manual"), tr("100 ms"), tr("500 ms"), tr("1 s")});
    connect(m_refreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PortMonitorWidget::onAutoRefreshChanged);
    m_toolbar->addWidget(m_refreshCombo);

    layout->addWidget(m_toolbar);

    /* Tree widget */
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Address"), tr("Value"), tr("Name"), tr("Decoded")});
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &PortMonitorWidget::onItemDoubleClicked);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &PortMonitorWidget::contextMenuAt);

    layout->addWidget(m_tree);

    /* Auto-refresh timer */
    m_autoRefreshTimer = new QTimer(this);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &PortMonitorWidget::refresh);
}

QTreeWidgetItem *PortMonitorWidget::findOrCreateGroup(const QString &group)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        auto *item = m_tree->topLevelItem(i);
        if (item->text(2) == group && item->data(0, Qt::UserRole).toUInt() == 0xFFFFFFFF)
            return item;
    }
    auto *groupItem = new QTreeWidgetItem(m_tree);
    groupItem->setText(2, group);
    groupItem->setData(0, Qt::UserRole, 0xFFFFFFFF);
    groupItem->setExpanded(true);

    QFont bold = groupItem->font(2);
    bold.setBold(true);
    groupItem->setFont(2, bold);

    return groupItem;
}

void PortMonitorWidget::addPortEntry(uint32_t addr, const QString &name,
                                      QTreeWidgetItem *parent)
{
    /* Check for duplicates across all items */
    auto checkDuplicate = [&](QTreeWidgetItem *root, int count) -> bool {
        for (int i = 0; i < count; i++) {
            auto *item = root ? root->child(i) : m_tree->topLevelItem(i);
            if (!item) continue;
            if (item->data(0, Qt::UserRole).toUInt() == addr)
                return true;
            /* Check children */
            for (int j = 0; j < item->childCount(); j++) {
                if (item->child(j)->data(0, Qt::UserRole).toUInt() == addr)
                    return true;
            }
        }
        return false;
    };

    if (checkDuplicate(nullptr, m_tree->topLevelItemCount()))
        return;

    QTreeWidgetItem *item;
    if (parent)
        item = new QTreeWidgetItem(parent);
    else
        item = new QTreeWidgetItem(m_tree);

    item->setText(0, QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
    item->setText(2, name);
    item->setData(0, Qt::UserRole, addr);
}

void PortMonitorWidget::refresh()
{
    if (!isVisible())
        return;

    std::function<void(QTreeWidgetItem *)> refreshItem = [&](QTreeWidgetItem *item) {
        uint32_t addr = item->data(0, Qt::UserRole).toUInt();
        if (addr == 0xFFFFFFFF) {
            /* Group node -- refresh children */
            for (int i = 0; i < item->childCount(); i++)
                refreshItem(item->child(i));
            return;
        }

        uint32_t val = 0;
        if (!debug_peek_reg(addr, &val)) {
            /* MMIO reads via debug_read_memory are only safe when the
             * emulator is paused in the debugger (in_debugger == true). */
            extern bool in_debugger;
            if (in_debugger)
                debug_read_memory(addr, &val, 4);
        }

        QString valStr = QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0'));
        item->setText(1, valStr);

        /* Decoded field */
        QString decoded = decodePeripheralValue(addr, val);
        if (!decoded.isEmpty()) {
            item->setText(3, decoded);
            item->setForeground(3, QBrush(currentWidgetTheme().syntaxSymbol));
        }

        /* Value column color */
        item->setForeground(0, QBrush(currentWidgetTheme().syntaxAddress));

        /* Highlight changes */
        auto it = m_prevValues.find(addr);
        if (it != m_prevValues.end() && it.value() != val) {
            item->setForeground(1, QBrush(currentWidgetTheme().changedValue));
        } else {
            item->setForeground(1, QBrush());
        }
        m_prevValues[addr] = val;
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        refreshItem(m_tree->topLevelItem(i));
}

QString PortMonitorWidget::decodePeripheralValue(uint32_t addr, uint32_t val) const
{
    const uint32_t lcd_control_addr = emulate_cx ? 0xC0000018u : 0xC000001Cu;
    const uint32_t lcd_int_mask_addr = emulate_cx ? 0xC000001Cu : 0xC0000018u;

    /* LCD Control register */
    if (addr == lcd_control_addr) {
        QStringList parts;
        int bpp_code = (val >> 1) & 7;
        const char *bpp_names[] = {"1bpp","2bpp","4bpp","8bpp","16bpp","24bpp","16bpp565","12bpp"};
        parts << QString::fromLatin1(bpp_names[bpp_code]);
        if (val & (1 << 5)) parts << QStringLiteral("TFT");
        else parts << QStringLiteral("STN");
        if (val & (1 << 11)) parts << QStringLiteral("Power=ON");
        else parts << QStringLiteral("Power=OFF");
        return parts.join(QStringLiteral(", "));
    }

    if (addr == lcd_int_mask_addr) {
        if (val == 0)
            return QStringLiteral("(none)");
        QStringList bits;
        if (val & 0x2u) bits << QStringLiteral("FUF");
        if (val & 0x4u) bits << QStringLiteral("LNB");
        if (val & 0x8u) bits << QStringLiteral("VCOMP");
        if (val & 0x10u) bits << QStringLiteral("BER");
        return bits.isEmpty() ? QStringLiteral("0x%1").arg(val, 8, 16, QLatin1Char('0'))
                              : bits.join(QStringLiteral(", "));
    }

    /* VIC IRQ/FIQ status */
    if (addr == 0xDC000000 || addr == 0xDC000004 || addr == 0xDC000008) {
        if (val == 0) return QStringLiteral("(none)");
        QStringList bits;
        for (int i = 0; i < 32; i++) {
            if (val & (1u << i))
                bits << QString::number(i);
        }
        return QStringLiteral("IRQs: ") + bits.join(QStringLiteral(","));
    }

    /* Timer Control */
    if (addr == 0x90010008 || addr == 0x900C0008) {
        QStringList parts;
        if (val & (1 << 7)) parts << QStringLiteral("Enabled");
        else parts << QStringLiteral("Disabled");
        if (val & (1 << 6)) parts << QStringLiteral("Periodic");
        else parts << QStringLiteral("FreeRun");
        int prescale = (val >> 2) & 3;
        const char *ps[] = {"div1","div16","div256","undef"};
        parts << QString::fromLatin1(ps[prescale]);
        if (val & (1 << 5)) parts << QStringLiteral("IE");
        return parts.join(QStringLiteral(", "));
    }

    /* UART Flags */
    if (addr == 0x90020018) {
        QStringList parts;
        if (val & (1 << 4)) parts << QStringLiteral("TX_EMPTY");
        if (val & (1 << 5)) parts << QStringLiteral("RX_FULL");
        if (val & (1 << 3)) parts << QStringLiteral("BUSY");
        if (val & (1 << 7)) parts << QStringLiteral("TX_FULL");
        if (val & (1 << 6)) parts << QStringLiteral("RX_EMPTY");
        return parts.join(QStringLiteral(", "));
    }

    /* Aladdin PMU Clocks */
    if (addr == 0x90140030) {
        uint32_t mult = (val >> 24) & 0x3F;
        uint32_t base_freq = mult * 12;
        return QStringLiteral("PLL=%1x12=%2 MHz, AHB=%3, APB=%4")
            .arg(mult).arg(base_freq).arg(base_freq / 2).arg(base_freq / 4);
    }

    /* GPIO Data */
    if ((addr & 0xFFFFF000) == 0x90000000 && (addr & 0xF) == 0) {
        return QStringLiteral("0b%1").arg(val, 32, 2, QLatin1Char('0'));
    }

    return {};
}

void PortMonitorWidget::addPort()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Port"));
    auto *form = new QFormLayout(&dlg);

    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setPlaceholderText(QStringLiteral("MMIO address (hex)"));
    form->addRow(tr("Address:"), addrEdit);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QStringLiteral("optional label"));
    form->addRow(tr("Name:"), nameEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        bool ok = false;
        uint32_t addr = addrEdit->text().toUInt(&ok, 16);
        if (ok) {
            addPortEntry(addr, nameEdit->text());
            refresh();
        }
    }
}

void PortMonitorWidget::removePort()
{
    auto *item = m_tree->currentItem();
    if (item)
        delete item;
}

void PortMonitorWidget::addCommonPorts()
{
    /* GPIO group */
    auto *gpio = findOrCreateGroup(QStringLiteral("GPIO"));
    addPortEntry(0x90000000, QStringLiteral("Port A Data"), gpio);
    addPortEntry(0x90000010, QStringLiteral("Port A Direction"), gpio);
    addPortEntry(0x90000800, QStringLiteral("Port J Data"), gpio);
    addPortEntry(0x90000810, QStringLiteral("Port J Direction"), gpio);

    /* Timers group */
    auto *timers = findOrCreateGroup(QStringLiteral("Timers"));
    addPortEntry(0x90010000, QStringLiteral("Fast Timer 0 Load"), timers);
    addPortEntry(0x90010004, QStringLiteral("Fast Timer 0 Value"), timers);
    addPortEntry(0x90010008, QStringLiteral("Fast Timer 0 Control"), timers);
    addPortEntry(0x900C0000, QStringLiteral("Watchdog Load"), timers);
    addPortEntry(0x900C0008, QStringLiteral("Watchdog Control"), timers);
    addPortEntry(0x900D0000, QStringLiteral("RTC Data"), timers);

    /* UART group */
    auto *uart = findOrCreateGroup(QStringLiteral("UART"));
    addPortEntry(0x90020000, QStringLiteral("UART1 Data"), uart);
    addPortEntry(0x90020018, QStringLiteral("UART1 Flags"), uart);

    /* LCD group */
    auto *lcd = findOrCreateGroup(QStringLiteral("LCD"));
    addPortEntry(0xC0000000, QStringLiteral("LCD Timing 0"), lcd);
    addPortEntry(0xC0000004, QStringLiteral("LCD Timing 1"), lcd);
    addPortEntry(0xC0000014, QStringLiteral("LCD Upper Panel Base"), lcd);
    if (emulate_cx) {
        addPortEntry(0xC0000018, QStringLiteral("LCD Control"), lcd);
        addPortEntry(0xC000001C, QStringLiteral("LCD Int Mask"), lcd);
    } else {
        addPortEntry(0xC0000018, QStringLiteral("LCD Int Mask"), lcd);
        addPortEntry(0xC000001C, QStringLiteral("LCD Control"), lcd);
    }

    /* Interrupt Controller group */
    auto *vic = findOrCreateGroup(QStringLiteral("Interrupt Controller"));
    addPortEntry(0xDC000000, QStringLiteral("VIC IRQ Status"), vic);
    addPortEntry(0xDC000004, QStringLiteral("VIC FIQ Status"), vic);
    addPortEntry(0xDC000008, QStringLiteral("VIC Raw Status"), vic);
    addPortEntry(0xDC00000C, QStringLiteral("VIC Int Select"), vic);
    addPortEntry(0xDC000010, QStringLiteral("VIC Int Enable"), vic);

    /* PMU group */
    auto *pmu = findOrCreateGroup(QStringLiteral("PMU"));
    addPortEntry(0x900B0000, QStringLiteral("ADC/PMU Control"), pmu);
    addPortEntry(0x90140000, QStringLiteral("Aladdin PMU Base"), pmu);
    addPortEntry(0x90140030, QStringLiteral("Aladdin PMU Clocks"), pmu);

    refresh();
}

void PortMonitorWidget::exportCSV()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Ports"),
        QString(), tr("CSV Files (*.csv)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << "Address,Value,Name,Decoded\n";

    std::function<void(QTreeWidgetItem *)> writeItem = [&](QTreeWidgetItem *item) {
        uint32_t addr = item->data(0, Qt::UserRole).toUInt();
        if (addr == 0xFFFFFFFF) {
            for (int i = 0; i < item->childCount(); i++)
                writeItem(item->child(i));
            return;
        }
        out << item->text(0) << "," << item->text(1) << ","
            << "\"" << item->text(2) << "\","
            << "\"" << item->text(3) << "\"\n";
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        writeItem(m_tree->topLevelItem(i));
}

void PortMonitorWidget::onAutoRefreshChanged(int index)
{
    m_autoRefreshTimer->stop();
    switch (index) {
    case 1: m_autoRefreshTimer->start(100); break;
    case 2: m_autoRefreshTimer->start(500); break;
    case 3: m_autoRefreshTimer->start(1000); break;
    default: break;
    }
}

void PortMonitorWidget::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    if (addr != 0xFFFFFFFF)
        emit goToAddress(addr);
}

void PortMonitorWidget::contextMenuAt(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item) return;
    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    if (addr == 0xFFFFFFFF) return;

    QMenu menu(this);

    menu.addAction(tr("Copy Address"), this, [addr]() {
        QApplication::clipboard()->setText(
            QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
    });

    menu.addAction(tr("Copy Value"), this, [item]() {
        QApplication::clipboard()->setText(item->text(1));
    });

    menu.addAction(tr("Go to Address in Memory"), this, [this, addr]() {
        emit goToAddress(addr);
    });

    menu.exec(m_tree->mapToGlobal(pos));
}
