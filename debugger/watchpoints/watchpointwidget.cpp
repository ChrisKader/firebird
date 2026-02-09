#include "watchpointwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QFont>
#include <QFontDatabase>

#include "core/debug_api.h"

/* Column indices */
enum { COL_ENABLED = 0, COL_ADDR, COL_SIZE, COL_READ, COL_WRITE, COL_VALUE };

WatchpointWidget::WatchpointWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("On"), tr("Address"), tr("Size"), tr("Read"),
                              tr("Write"), tr("Value")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(COL_ENABLED, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_ADDR, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(COL_SIZE, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_READ, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_WRITE, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_VALUE, QHeaderView::ResizeToContents);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_tree->setFont(mono);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &WatchpointWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &WatchpointWidget::onItemChanged);
    layout->addWidget(m_tree);

    /* Toolbar */
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    QAction *addAct = m_toolbar->addAction(QStringLiteral("+"));
    addAct->setToolTip(tr("Add Watchpoint"));
    connect(addAct, &QAction::triggered, this, &WatchpointWidget::addWatchpoint);

    QAction *removeAct = m_toolbar->addAction(QStringLiteral("\u2212"));
    removeAct->setToolTip(tr("Remove Watchpoint"));
    connect(removeAct, &QAction::triggered, this, &WatchpointWidget::removeWatchpoint);

    layout->addWidget(m_toolbar);
}

void WatchpointWidget::refresh()
{
    m_refreshing = true;
    m_tree->clear();

    struct debug_breakpoint bps[512];
    int count = debug_list_breakpoints(bps, 512);

    for (int i = 0; i < count; i++) {
        if (!bps[i].read && !bps[i].write)
            continue; /* skip exec-only breakpoints */

        auto *item = new QTreeWidgetItem(m_tree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(COL_ENABLED, bps[i].enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(COL_ADDR, QStringLiteral("%1").arg(bps[i].addr, 8, 16, QLatin1Char('0')));
        item->setText(COL_SIZE, QString::number(bps[i].size));
        item->setText(COL_READ,  bps[i].read  ? QStringLiteral("\u2713") : QString());
        item->setText(COL_WRITE, bps[i].write ? QStringLiteral("\u2713") : QString());
        item->setData(COL_ADDR, Qt::UserRole, bps[i].addr);

        /* Read current value at watched address */
        uint32_t val = 0;
        int readSize = qMin(bps[i].size, 4u);
        debug_read_memory(bps[i].addr, &val, readSize);
        item->setText(COL_VALUE, QStringLiteral("%1").arg(val, readSize * 2, 16, QLatin1Char('0')));
    }
    m_refreshing = false;
}

void WatchpointWidget::addWatchpoint()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Watchpoint"));
    auto *form = new QFormLayout(&dlg);

    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setPlaceholderText(QStringLiteral("hex address"));
    form->addRow(tr("Address:"), addrEdit);

    auto *sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(1, 256);
    sizeSpin->setValue(4);
    form->addRow(tr("Size (bytes):"), sizeSpin);

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

    uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
    debug_clear_breakpoint(addr);
    refresh();
}

void WatchpointWidget::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
    emit goToAddress(addr);
}

void WatchpointWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_refreshing || !item)
        return;

    if (column == COL_ENABLED) {
        uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
        bool enabled = (item->checkState(COL_ENABLED) == Qt::Checked);
        debug_set_breakpoint_enabled(addr, enabled);
        if (enabled)
            debug_set_breakpoint(addr, false, true, true);
        refresh();
    }
}
