#include "watchpointwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

#include "core/debug_api.h"

WatchpointWidget::WatchpointWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Address"), tr("Read"), tr("Write")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &WatchpointWidget::onItemDoubleClicked);
    layout->addWidget(m_tree);

    auto *btnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addBtn, &QPushButton::clicked, this, &WatchpointWidget::addWatchpoint);
    connect(m_removeBtn, &QPushButton::clicked, this, &WatchpointWidget::removeWatchpoint);
}

void WatchpointWidget::refresh()
{
    m_tree->clear();

    struct debug_breakpoint bps[512];
    int count = debug_list_breakpoints(bps, 512);

    for (int i = 0; i < count; i++) {
        if (!bps[i].read && !bps[i].write)
            continue; /* skip exec-only breakpoints */

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("%1").arg(bps[i].addr, 8, 16, QLatin1Char('0')));
        item->setText(1, bps[i].read ? QStringLiteral("\u2713") : QString());
        item->setText(2, bps[i].write ? QStringLiteral("\u2713") : QString());
        item->setData(0, Qt::UserRole, bps[i].addr);
    }
}

void WatchpointWidget::addWatchpoint()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Watchpoint"));
    auto *form = new QFormLayout(&dlg);

    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setPlaceholderText(QStringLiteral("hex address"));
    form->addRow(tr("Address:"), addrEdit);

    auto *readBox = new QCheckBox(tr("Read"), &dlg);
    readBox->setChecked(true);
    auto *writeBox = new QCheckBox(tr("Write"), &dlg);
    writeBox->setChecked(true);
    form->addRow(tr("Type:"), readBox);
    form->addRow(QString(), writeBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        bool ok = false;
        uint32_t addr = addrEdit->text().toUInt(&ok, 16);
        if (ok) {
            debug_set_breakpoint(addr, false,
                                 readBox->isChecked(), writeBox->isChecked());
            refresh();
        }
    }
}

void WatchpointWidget::removeWatchpoint()
{
    auto *item = m_tree->currentItem();
    if (!item)
        return;

    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    debug_clear_breakpoint(addr);
    refresh();
}

void WatchpointWidget::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    uint32_t addr = item->data(0, Qt::UserRole).toUInt();
    emit goToAddress(addr);
}
