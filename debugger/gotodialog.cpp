#include "gotodialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>

GoToDialog::GoToDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Go To Address"));
    setMinimumWidth(280);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);

    m_addrEdit = new QLineEdit(this);
    m_addrEdit->setFont(mono);
    m_addrEdit->setPlaceholderText(QStringLiteral("hex address, e.g. 10000000"));
    m_addrEdit->setMaxLength(8);
    form->addRow(tr("Address:"), m_addrEdit);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem(tr("Disassembly"), Disassembly);
    m_targetCombo->addItem(tr("Memory"), Memory);
    form->addRow(tr("View in:"), m_targetCombo);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_addrEdit->setFocus();
}

uint32_t GoToDialog::getAddress() const
{
    bool ok = false;
    uint32_t addr = m_addrEdit->text().toUInt(&ok, 16);
    return ok ? addr : 0;
}

GoToDialog::Target GoToDialog::getTarget() const
{
    return static_cast<Target>(m_targetCombo->currentData().toInt());
}
