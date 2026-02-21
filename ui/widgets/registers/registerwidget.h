#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QFont>
#include <QComboBox>
#include <QTreeWidget>
#include <stdint.h>

#include "ui/docking/dockstate.h"

class RegisterWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget *parent = nullptr);

    enum DisplayFormat { FormatHex = 0, FormatDecimal, FormatBinary };

public slots:
    void refresh();
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

signals:
    void registerChanged(int reg, uint32_t value);
    void goToAddress(uint32_t addr);
    void gotoDisassembly(uint32_t addr);

private:
    void commitRegister(int reg);
    void commitCpsr();
    void refreshBankedRegisters();
    void refreshCP15();
    QString formatValue(uint32_t val) const;
    void showContextMenu(QLineEdit *edit, uint32_t value, const QPoint &pos);

    QLineEdit *m_regEdits[16] = {};
    QLineEdit *m_cpsrEdit = nullptr;
    QLineEdit *m_spsrEdit = nullptr;
    QLabel *m_modeLabel = nullptr;

    QCheckBox *m_flagN = nullptr;
    QCheckBox *m_flagZ = nullptr;
    QCheckBox *m_flagC = nullptr;
    QCheckBox *m_flagV = nullptr;
    QCheckBox *m_flagT = nullptr;
    QCheckBox *m_flagI = nullptr;
    QCheckBox *m_flagF = nullptr;

    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;

    /* Banked registers section */
    QTreeWidget *m_bankedTree = nullptr;

    /* CP15 section */
    QTreeWidget *m_cp15Tree = nullptr;

    uint32_t m_prevRegs[16] = {};
    uint32_t m_prevCpsr = 0;
    bool m_hasPrev = false;

    QFont m_monoFont;
};

#endif // REGISTERWIDGET_H
