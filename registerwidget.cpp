#include "registerwidget.h"

#include <QGridLayout>
#include <QFontDatabase>
#include <QPalette>

#include "core/debug_api.h"

static const char *reg_names[16] = {
    "r0", "r1", "r2",  "r3",  "r4",  "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"
};

static const char *mode_name(uint32_t mode)
{
    switch (mode & 0x1F) {
    case 0x10: return "USR";
    case 0x11: return "FIQ";
    case 0x12: return "IRQ";
    case 0x13: return "SVC";
    case 0x17: return "ABT";
    case 0x1B: return "UND";
    case 0x1F: return "SYS";
    default:   return "???";
    }
}

RegisterWidget::RegisterWidget(QWidget *parent)
    : QWidget(parent)
{
    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_monoFont.setPointSize(11);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);

    /* Create register edit fields: 2 columns of 8 registers */
    for (int i = 0; i < 16; i++) {
        int col = (i / 8) * 2;
        int row = i % 8;

        auto *label = new QLabel(QString::fromLatin1(reg_names[i]), this);
        label->setFont(m_monoFont);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label, row, col);

        auto *edit = new QLineEdit(this);
        edit->setFont(m_monoFont);
        edit->setMaxLength(8);
        edit->setFixedWidth(80);
        edit->setAlignment(Qt::AlignRight);
        grid->addWidget(edit, row, col + 1);
        m_regEdits[i] = edit;

        connect(edit, &QLineEdit::returnPressed, this, [this, i]() {
            commitRegister(i);
        });
    }

    /* CPSR row */
    int row = 8;
    auto *cpsrLabel = new QLabel(QStringLiteral("cpsr"), this);
    cpsrLabel->setFont(m_monoFont);
    cpsrLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(cpsrLabel, row, 0);

    m_cpsrEdit = new QLineEdit(this);
    m_cpsrEdit->setFont(m_monoFont);
    m_cpsrEdit->setMaxLength(8);
    m_cpsrEdit->setFixedWidth(80);
    m_cpsrEdit->setAlignment(Qt::AlignRight);
    grid->addWidget(m_cpsrEdit, row, 1);
    connect(m_cpsrEdit, &QLineEdit::returnPressed, this, &RegisterWidget::commitCpsr);

    /* SPSR */
    auto *spsrLabel = new QLabel(QStringLiteral("spsr"), this);
    spsrLabel->setFont(m_monoFont);
    spsrLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(spsrLabel, row, 2);

    m_spsrEdit = new QLineEdit(this);
    m_spsrEdit->setFont(m_monoFont);
    m_spsrEdit->setMaxLength(8);
    m_spsrEdit->setFixedWidth(80);
    m_spsrEdit->setAlignment(Qt::AlignRight);
    m_spsrEdit->setReadOnly(true);
    grid->addWidget(m_spsrEdit, row, 3);

    /* Flags row */
    row++;
    auto *flagsWidget = new QWidget(this);
    auto *flagsLayout = new QGridLayout(flagsWidget);
    flagsLayout->setContentsMargins(0, 0, 0, 0);
    flagsLayout->setSpacing(4);

    m_flagN = new QCheckBox(QStringLiteral("N"), flagsWidget);
    m_flagZ = new QCheckBox(QStringLiteral("Z"), flagsWidget);
    m_flagC = new QCheckBox(QStringLiteral("C"), flagsWidget);
    m_flagV = new QCheckBox(QStringLiteral("V"), flagsWidget);
    m_flagT = new QCheckBox(QStringLiteral("T"), flagsWidget);
    m_flagI = new QCheckBox(QStringLiteral("I"), flagsWidget);
    m_flagF = new QCheckBox(QStringLiteral("F"), flagsWidget);

    flagsLayout->addWidget(m_flagN, 0, 0);
    flagsLayout->addWidget(m_flagZ, 0, 1);
    flagsLayout->addWidget(m_flagC, 0, 2);
    flagsLayout->addWidget(m_flagV, 0, 3);
    flagsLayout->addWidget(m_flagT, 0, 4);
    flagsLayout->addWidget(m_flagI, 0, 5);
    flagsLayout->addWidget(m_flagF, 0, 6);

    grid->addWidget(flagsWidget, row, 0, 1, 4);

    /* Mode label */
    row++;
    m_modeLabel = new QLabel(this);
    m_modeLabel->setFont(m_monoFont);
    grid->addWidget(m_modeLabel, row, 0, 1, 4);

    grid->setRowStretch(row + 1, 1);
}

void RegisterWidget::refresh()
{
    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;
    debug_get_registers(regs, &cpsr, &spsr, &has_spsr);

    QPalette normalPal = palette();
    QPalette changedPal = normalPal;
    changedPal.setColor(QPalette::Text, Qt::red);

    for (int i = 0; i < 16; i++) {
        m_regEdits[i]->setText(QStringLiteral("%1").arg(regs[i], 8, 16, QLatin1Char('0')));
        if (m_hasPrev && regs[i] != m_prevRegs[i])
            m_regEdits[i]->setPalette(changedPal);
        else
            m_regEdits[i]->setPalette(normalPal);
    }

    m_cpsrEdit->setText(QStringLiteral("%1").arg(cpsr, 8, 16, QLatin1Char('0')));
    if (m_hasPrev && cpsr != m_prevCpsr)
        m_cpsrEdit->setPalette(changedPal);
    else
        m_cpsrEdit->setPalette(normalPal);

    if (has_spsr) {
        m_spsrEdit->setText(QStringLiteral("%1").arg(spsr, 8, 16, QLatin1Char('0')));
        m_spsrEdit->setEnabled(true);
    } else {
        m_spsrEdit->setText(QStringLiteral("--------"));
        m_spsrEdit->setEnabled(false);
    }

    /* Update flag checkboxes (without triggering signals) */
    m_flagN->blockSignals(true);
    m_flagZ->blockSignals(true);
    m_flagC->blockSignals(true);
    m_flagV->blockSignals(true);
    m_flagT->blockSignals(true);
    m_flagI->blockSignals(true);
    m_flagF->blockSignals(true);

    m_flagN->setChecked(cpsr & (1u << 31));
    m_flagZ->setChecked(cpsr & (1u << 30));
    m_flagC->setChecked(cpsr & (1u << 29));
    m_flagV->setChecked(cpsr & (1u << 28));
    m_flagT->setChecked(cpsr & (1u << 5));
    m_flagI->setChecked(cpsr & (1u << 7));
    m_flagF->setChecked(cpsr & (1u << 6));

    m_flagN->blockSignals(false);
    m_flagZ->blockSignals(false);
    m_flagC->blockSignals(false);
    m_flagV->blockSignals(false);
    m_flagT->blockSignals(false);
    m_flagI->blockSignals(false);
    m_flagF->blockSignals(false);

    m_modeLabel->setText(QStringLiteral("Mode: %1").arg(QString::fromLatin1(mode_name(cpsr))));

    /* Save for next refresh diff */
    memcpy(m_prevRegs, regs, sizeof(m_prevRegs));
    m_prevCpsr = cpsr;
    m_hasPrev = true;
}

void RegisterWidget::commitRegister(int reg)
{
    bool ok = false;
    uint32_t val = m_regEdits[reg]->text().toUInt(&ok, 16);
    if (ok) {
        debug_set_register(reg, val);
        emit registerChanged(reg, val);
    }
}

void RegisterWidget::commitCpsr()
{
    bool ok = false;
    uint32_t val = m_cpsrEdit->text().toUInt(&ok, 16);
    if (ok)
        debug_set_cpsr(val);
}
