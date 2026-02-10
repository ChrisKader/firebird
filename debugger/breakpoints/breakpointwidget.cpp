#include "breakpointwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QFont>
#include <QFontDatabase>
#include <QMenu>
#include <QMessageBox>

#include "core/debug_api.h"

/* Column indices */
enum { COL_ENABLED = 0, COL_ADDR, COL_EXEC, COL_READ, COL_WRITE, COL_HITS, COL_CONDITION };

BreakpointWidget::BreakpointWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("On"), tr("Address"), tr("Exec"), tr("Read"),
                              tr("Write"), tr("Hits"), tr("Condition")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(COL_ENABLED, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_ADDR, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(COL_EXEC, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_READ, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_WRITE, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_HITS, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(COL_CONDITION, QHeaderView::Stretch);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_tree->setFont(mono);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &BreakpointWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &BreakpointWidget::onItemChanged);
    layout->addWidget(m_tree);

    /* Toolbar with icon-style buttons */
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    QAction *addAct = m_toolbar->addAction(QStringLiteral("+"));
    addAct->setToolTip(tr("Add Breakpoint"));
    connect(addAct, &QAction::triggered, this, &BreakpointWidget::addBreakpoint);

    QAction *removeAct = m_toolbar->addAction(QStringLiteral("\u2212"));
    removeAct->setToolTip(tr("Remove Breakpoint"));
    connect(removeAct, &QAction::triggered, this, &BreakpointWidget::removeBreakpoint);

    QAction *clearAct = m_toolbar->addAction(QStringLiteral("\u2717"));
    clearAct->setToolTip(tr("Clear All"));
    connect(clearAct, &QAction::triggered, this, &BreakpointWidget::removeAll);

    layout->addWidget(m_toolbar);
}

void BreakpointWidget::refresh()
{
    m_refreshing = true;
    m_tree->clear();

    struct debug_breakpoint bps[512];
    int count = debug_list_breakpoints(bps, 512);

    for (int i = 0; i < count; i++) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(COL_ENABLED, bps[i].enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(COL_ADDR, QStringLiteral("%1").arg(bps[i].addr, 8, 16, QLatin1Char('0')));
        item->setText(COL_EXEC,  bps[i].exec  ? QStringLiteral("\u2713") : QString());
        item->setText(COL_READ,  bps[i].read  ? QStringLiteral("\u2713") : QString());
        item->setText(COL_WRITE, bps[i].write ? QStringLiteral("\u2713") : QString());
        item->setText(COL_HITS, QString::number(bps[i].hit_count));
        const char *cond = debug_get_breakpoint_condition(bps[i].addr);
        item->setText(COL_CONDITION, cond ? QString::fromUtf8(cond) : QString());
        item->setData(COL_ADDR, Qt::UserRole, bps[i].addr);
        item->setData(COL_ADDR, Qt::UserRole + 1, bps[i].exec);
        item->setData(COL_ADDR, Qt::UserRole + 2, bps[i].read);
        item->setData(COL_ADDR, Qt::UserRole + 3, bps[i].write);
        item->setData(COL_ADDR, Qt::UserRole + 4, bps[i].enabled);
    }
    m_refreshing = false;
}

void BreakpointWidget::addBreakpoint()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Breakpoint"));
    auto *form = new QFormLayout(&dlg);

    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setPlaceholderText(QStringLiteral("hex address"));
    form->addRow(tr("Address:"), addrEdit);

    auto *execBox = new QCheckBox(tr("Execute"), &dlg);
    execBox->setChecked(true);
    auto *readBox = new QCheckBox(tr("Read"), &dlg);
    auto *writeBox = new QCheckBox(tr("Write"), &dlg);
    form->addRow(tr("Type:"), execBox);
    form->addRow(QString(), readBox);
    form->addRow(QString(), writeBox);

    auto *condEdit = new QLineEdit(&dlg);
    condEdit->setPlaceholderText(QStringLiteral("e.g. r0==0x1234, hit>=5, [0xA0000000]==0xFF"));
    form->addRow(tr("Condition:"), condEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        QString text = addrEdit->text().trimmed();
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            text = text.mid(2);
        bool ok = false;
        uint32_t addr = text.toUInt(&ok, 16);
        if (!ok || text.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid Address"),
                                 tr("Please enter a valid hex address."));
            return;
        }
        if (!debug_set_breakpoint(addr, execBox->isChecked(),
                                  readBox->isChecked(), writeBox->isChecked())) {
            QMessageBox::warning(this, tr("Breakpoint Failed"),
                                 tr("Could not set breakpoint at 0x%1.\n"
                                    "The address may not be in RAM.")
                                     .arg(addr, 8, 16, QLatin1Char('0')));
            return;
        }
        if (!condEdit->text().isEmpty())
            debug_set_breakpoint_condition(addr, condEdit->text().toUtf8().constData());
        refresh();
    }
}

void BreakpointWidget::removeBreakpoint()
{
    auto *item = m_tree->currentItem();
    if (!item)
        return;

    uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
    debug_clear_breakpoint(addr);
    refresh();
}

void BreakpointWidget::removeAll()
{
    if (m_tree->topLevelItemCount() == 0)
        return;

    auto reply = QMessageBox::question(this, tr("Clear All Breakpoints"),
                                        tr("Remove all breakpoints?"),
                                        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    struct debug_breakpoint bps[512];
    int count = debug_list_breakpoints(bps, 512);
    for (int i = 0; i < count; i++) {
        debug_clear_breakpoint(bps[i].addr);
    }
    refresh();
}

void BreakpointWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    if (!item)
        return;

    /* Double-click on hits column resets the counter */
    if (column == COL_HITS) {
        uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
        debug_reset_hit_count(addr);
        refresh();
        return;
    }

    /* Double-click on condition column to edit condition */
    if (column == COL_CONDITION) {
        uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Edit Condition"));
        auto *form = new QFormLayout(&dlg);
        auto *condEdit = new QLineEdit(&dlg);
        condEdit->setText(item->text(COL_CONDITION));
        condEdit->setPlaceholderText(QStringLiteral("e.g. r0==0x1234, hit>=5"));
        form->addRow(tr("Condition:"), condEdit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted) {
            debug_set_breakpoint_condition(addr, condEdit->text().toUtf8().constData());
            refresh();
        }
        return;
    }

    uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
    emit goToAddress(addr);
}

void BreakpointWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_refreshing || !item)
        return;

    /* Handle enable/disable checkbox toggle */
    if (column == COL_ENABLED) {
        uint32_t addr = item->data(COL_ADDR, Qt::UserRole).toUInt();
        bool enabled = (item->checkState(COL_ENABLED) == Qt::Checked);
        bool wasEnabled = item->data(COL_ADDR, Qt::UserRole + 4).toBool();

        if (enabled != wasEnabled) {
            if (enabled) {
                /* Re-enable: re-set the breakpoint with original flags */
                bool exec = item->data(COL_ADDR, Qt::UserRole + 1).toBool();
                bool read = item->data(COL_ADDR, Qt::UserRole + 2).toBool();
                bool write = item->data(COL_ADDR, Qt::UserRole + 3).toBool();
                debug_set_breakpoint(addr, exec, read, write);
            }
            debug_set_breakpoint_enabled(addr, enabled);
            refresh();
        }
    }
}
