#include "portmonitorwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

#include "core/debug_api.h"

PortMonitorWidget::PortMonitorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Address"), tr("Value"), tr("Name")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &PortMonitorWidget::onItemDoubleClicked);
    layout->addWidget(m_tree);

    auto *btnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_commonBtn = new QPushButton(tr("Common"), this);
    m_commonBtn->setToolTip(tr("Add common TI-Nspire MMIO ports"));
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addWidget(m_commonBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addBtn, &QPushButton::clicked, this, &PortMonitorWidget::addPort);
    connect(m_removeBtn, &QPushButton::clicked, this, &PortMonitorWidget::removePort);
    connect(m_commonBtn, &QPushButton::clicked, this, &PortMonitorWidget::addCommonPorts);
}

void PortMonitorWidget::addPortEntry(uint32_t addr, const QString &name)
{
    /* Check for duplicates */
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        if (m_tree->topLevelItem(i)->data(0, Qt::UserRole).toUInt() == addr)
            return;
    }

    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
    item->setText(2, name);
    item->setData(0, Qt::UserRole, addr);
}

void PortMonitorWidget::refresh()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        auto *item = m_tree->topLevelItem(i);
        uint32_t addr = item->data(0, Qt::UserRole).toUInt();

        uint32_t val = 0;
        debug_read_memory(addr, &val, 4);
        item->setText(1, QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0')));
    }
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
    /* TI-Nspire CX / CX II common MMIO registers */
    addPortEntry(0x90000000, QStringLiteral("GPIO Port A Data"));
    addPortEntry(0x90000010, QStringLiteral("GPIO Port A Direction"));
    addPortEntry(0x90010000, QStringLiteral("Timer 0 Load"));
    addPortEntry(0x90010004, QStringLiteral("Timer 0 Value"));
    addPortEntry(0x90010008, QStringLiteral("Timer 0 Control"));
    addPortEntry(0x90020000, QStringLiteral("UART1 Data"));
    addPortEntry(0x90020018, QStringLiteral("UART1 Flags"));
    addPortEntry(0x900B0000, QStringLiteral("ADC/PMU Control"));
    addPortEntry(0x900C0000, QStringLiteral("Watchdog Load"));
    addPortEntry(0x900C0008, QStringLiteral("Watchdog Control"));
    addPortEntry(0x900D0000, QStringLiteral("RTC Data"));
    addPortEntry(0x90140000, QStringLiteral("Aladdin PMU Base"));
    addPortEntry(0x90140030, QStringLiteral("Aladdin PMU Clocks"));
    addPortEntry(0xC0000000, QStringLiteral("LCD Timing 0"));
    addPortEntry(0xC0000004, QStringLiteral("LCD Timing 1"));
    addPortEntry(0xC0000014, QStringLiteral("LCD Upper Panel Base"));
    addPortEntry(0xC000001C, QStringLiteral("LCD Control"));
    addPortEntry(0xDC000000, QStringLiteral("VIC IRQ Status"));
    addPortEntry(0xDC000004, QStringLiteral("VIC FIQ Status"));
    addPortEntry(0xDC000008, QStringLiteral("VIC Raw Status"));
    addPortEntry(0xDC00000C, QStringLiteral("VIC Int Select"));
    addPortEntry(0xDC000010, QStringLiteral("VIC Int Enable"));
    refresh();
}

void PortMonitorWidget::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    emit goToAddress(addr);
}
