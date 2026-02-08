#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QFont>
#include <stdint.h>

class RegisterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void registerChanged(int reg, uint32_t value);

private:
    void commitRegister(int reg);
    void commitCpsr();

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

    uint32_t m_prevRegs[16] = {};
    uint32_t m_prevCpsr = 0;
    bool m_hasPrev = false;

    QFont m_monoFont;
};

#endif // REGISTERWIDGET_H
