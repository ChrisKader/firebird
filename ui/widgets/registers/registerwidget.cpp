#include "registerwidget.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFontDatabase>
#include <QPalette>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QSignalBlocker>

#include "core/debug_api.h"
#include "ui/theme/widgettheme.h"

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

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    /* Format and mode combos at top */
    auto *topLayout = new QGridLayout;
    topLayout->setSpacing(4);

    auto *fmtLabel = new QLabel(tr("Format:"), this);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItems({tr("Hex"), tr("Decimal"), tr("Binary")});
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refresh(); });
    topLayout->addWidget(fmtLabel, 0, 0);
    topLayout->addWidget(m_formatCombo, 0, 1);

    auto *bankLabel = new QLabel(tr("Mode:"), this);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({tr("Current"), tr("USR"), tr("FIQ"), tr("IRQ"),
                            tr("SVC"), tr("ABT"), tr("UND")});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refresh(); });
    topLayout->addWidget(bankLabel, 0, 2);
    topLayout->addWidget(m_modeCombo, 0, 3);
    mainLayout->addLayout(topLayout);

    /* Register edit fields: 2 columns of 8 registers */
    auto *regGrid = new QGridLayout;
    regGrid->setContentsMargins(0, 0, 0, 0);
    regGrid->setSpacing(2);

    for (int i = 0; i < 16; i++) {
        int col = (i / 8) * 2;
        int row = i % 8;

        auto *label = new QLabel(QString::fromLatin1(reg_names[i]), this);
        label->setFont(m_monoFont);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        regGrid->addWidget(label, row, col);

        auto *edit = new QLineEdit(this);
        edit->setFont(m_monoFont);
        edit->setMaxLength(32);
        edit->setAlignment(Qt::AlignRight);
        regGrid->addWidget(edit, row, col + 1);
        m_regEdits[i] = edit;

        connect(edit, &QLineEdit::returnPressed, this, [this, i]() {
            commitRegister(i);
        });

        edit->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(edit, &QLineEdit::customContextMenuRequested, this, [this, edit](const QPoint &pos) {
            bool ok;
            QString text = edit->text();
            text.remove(QLatin1Char(' '));
            uint32_t val = text.toUInt(&ok, 16);
            if (!ok) val = edit->text().toUInt(&ok, 10);
            if (!ok) val = 0;
            showContextMenu(edit, val, edit->mapToGlobal(pos));
        });
    }
    mainLayout->addLayout(regGrid);

    /* CPSR row */
    auto *cpsrLayout = new QGridLayout;
    cpsrLayout->setSpacing(2);

    auto *cpsrLabel = new QLabel(QStringLiteral("cpsr"), this);
    cpsrLabel->setFont(m_monoFont);
    cpsrLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cpsrLayout->addWidget(cpsrLabel, 0, 0);

    m_cpsrEdit = new QLineEdit(this);
    m_cpsrEdit->setFont(m_monoFont);
    m_cpsrEdit->setMaxLength(8);
    m_cpsrEdit->setAlignment(Qt::AlignRight);
    cpsrLayout->addWidget(m_cpsrEdit, 0, 1);
    connect(m_cpsrEdit, &QLineEdit::returnPressed, this, &RegisterWidget::commitCpsr);

    auto *spsrLabel = new QLabel(QStringLiteral("spsr"), this);
    spsrLabel->setFont(m_monoFont);
    spsrLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cpsrLayout->addWidget(spsrLabel, 0, 2);

    m_spsrEdit = new QLineEdit(this);
    m_spsrEdit->setFont(m_monoFont);
    m_spsrEdit->setMaxLength(8);
    m_spsrEdit->setAlignment(Qt::AlignRight);
    m_spsrEdit->setReadOnly(true);
    cpsrLayout->addWidget(m_spsrEdit, 0, 3);
    mainLayout->addLayout(cpsrLayout);

    /* Flags row */
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
    mainLayout->addWidget(flagsWidget);

    /* Mode label */
    m_modeLabel = new QLabel(this);
    m_modeLabel->setFont(m_monoFont);
    mainLayout->addWidget(m_modeLabel);

    /* CP15 section */
    auto *cp15Group = new QGroupBox(tr("CP15"), this);
    auto *cp15Layout = new QVBoxLayout(cp15Group);
    cp15Layout->setContentsMargins(2, 2, 2, 2);
    m_cp15Tree = new QTreeWidget(cp15Group);
    m_cp15Tree->setHeaderLabels({tr("Register"), tr("Value")});
    m_cp15Tree->setRootIsDecorated(false);
    m_cp15Tree->setFont(m_monoFont);
    m_cp15Tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_cp15Tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_cp15Tree->setMaximumHeight(160);
    cp15Layout->addWidget(m_cp15Tree);
    mainLayout->addWidget(cp15Group);

    mainLayout->addStretch(1);
}

QString RegisterWidget::formatValue(uint32_t val) const
{
    switch (m_formatCombo->currentIndex()) {
    case FormatDecimal:
        return QString::number(val);
    case FormatBinary:
        return QStringLiteral("%1").arg(val, 32, 2, QLatin1Char('0'));
    default: /* FormatHex */
        return QStringLiteral("%1").arg(val, 8, 16, QLatin1Char('0'));
    }
}

void RegisterWidget::refresh()
{
    int modeIdx = m_modeCombo->currentIndex();

    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;

    if (modeIdx == 0) {
        /* Current mode */
        debug_get_registers(regs, &cpsr, &spsr, &has_spsr);
    } else {
        /* Banked mode */
        static const uint32_t modes[] = { 0x10, 0x11, 0x12, 0x13, 0x17, 0x1B };
        uint32_t targetMode = modes[modeIdx - 1];
        debug_get_banked_registers(targetMode, regs, &spsr);
        /* Read current CPSR (still needed for flags display) */
        uint32_t dummy[16];
        uint32_t dummySpsr;
        bool dummyHas;
        debug_get_registers(dummy, &cpsr, &dummySpsr, &dummyHas);
        has_spsr = (targetMode != 0x10 && targetMode != 0x1F);
    }

    QPalette normalPal = palette();
    QPalette changedPal = normalPal;
    changedPal.setColor(QPalette::Text, currentWidgetTheme().changedValue);

    bool isBanked = (modeIdx != 0);

    for (int i = 0; i < 16; i++) {
        m_regEdits[i]->setText(formatValue(regs[i]));
        m_regEdits[i]->setReadOnly(isBanked);
        if (!isBanked && m_hasPrev && regs[i] != m_prevRegs[i])
            m_regEdits[i]->setPalette(changedPal);
        else
            m_regEdits[i]->setPalette(normalPal);
    }

    m_cpsrEdit->setText(QStringLiteral("%1").arg(cpsr, 8, 16, QLatin1Char('0')));
    if (!isBanked && m_hasPrev && cpsr != m_prevCpsr)
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

    /* Update flag checkboxes */
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

    /* CP15 */
    refreshCP15();

    /* Save for next refresh diff (only when viewing current) */
    if (!isBanked) {
        memcpy(m_prevRegs, regs, sizeof(m_prevRegs));
        m_prevCpsr = cpsr;
        m_hasPrev = true;
    }
}

QJsonObject RegisterWidget::serializeState() const
{
    QJsonObject state;
    if (m_formatCombo)
        state.insert(QStringLiteral("displayFormat"), m_formatCombo->currentIndex());
    if (m_modeCombo)
        state.insert(QStringLiteral("modeIndex"), m_modeCombo->currentIndex());
    return state;
}

void RegisterWidget::restoreState(const QJsonObject &state)
{
    if (m_formatCombo) {
        const QSignalBlocker block(*m_formatCombo);
        const int idx = state.value(QStringLiteral("displayFormat")).toInt(m_formatCombo->currentIndex());
        if (idx >= 0 && idx < m_formatCombo->count())
            m_formatCombo->setCurrentIndex(idx);
    }
    if (m_modeCombo) {
        const QSignalBlocker block(*m_modeCombo);
        const int idx = state.value(QStringLiteral("modeIndex")).toInt(m_modeCombo->currentIndex());
        if (idx >= 0 && idx < m_modeCombo->count())
            m_modeCombo->setCurrentIndex(idx);
    }
}

void RegisterWidget::refreshCP15()
{
    m_cp15Tree->clear();

    uint32_t cp15[6];
    debug_get_cp15(cp15);

    static const char *names[] = { "SCTLR", "TTBR0", "DACR", "DFSR", "IFSR", "FAR" };
    for (int i = 0; i < 6; i++) {
        auto *item = new QTreeWidgetItem(m_cp15Tree);
        item->setText(0, QString::fromLatin1(names[i]));
        item->setText(1, QStringLiteral("%1").arg(cp15[i], 8, 16, QLatin1Char('0')));
    }
}

void RegisterWidget::showContextMenu(QLineEdit *edit, uint32_t value, const QPoint &pos)
{
    QMenu menu(this);

    menu.addAction(tr("Copy Value"), this, [edit]() {
        QApplication::clipboard()->setText(edit->text().trimmed());
    });

    menu.addAction(tr("Copy All Registers"), this, []() {
        QString text;
        uint32_t regs[16];
        uint32_t cpsr, spsr;
        bool has_spsr;
        debug_get_registers(regs, &cpsr, &spsr, &has_spsr);
        for (int i = 0; i < 16; i++)
            text += QStringLiteral("%1 = %2\n")
                .arg(QString::fromLatin1(reg_names[i]), -4)
                .arg(regs[i], 8, 16, QLatin1Char('0'));
        text += QStringLiteral("cpsr = %1\n").arg(cpsr, 8, 16, QLatin1Char('0'));
        if (has_spsr)
            text += QStringLiteral("spsr = %1\n").arg(spsr, 8, 16, QLatin1Char('0'));
        QApplication::clipboard()->setText(text);
    });

    menu.addSeparator();

    menu.addAction(tr("Go to Address in Memory"), this, [this, value]() {
        emit goToAddress(value);
    });

    menu.addAction(tr("Go to Address in Disassembly"), this, [this, value]() {
        emit gotoDisassembly(value);
    });

    menu.exec(pos);
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
