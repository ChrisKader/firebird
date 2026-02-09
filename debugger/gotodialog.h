#ifndef GOTODIALOG_H
#define GOTODIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <stdint.h>

class GoToDialog : public QDialog
{
    Q_OBJECT

public:
    enum Target { Disassembly = 0, Memory = 1 };

    explicit GoToDialog(QWidget *parent = nullptr);

    uint32_t getAddress() const;
    Target getTarget() const;

private:
    QLineEdit *m_addrEdit = nullptr;
    QComboBox *m_targetCombo = nullptr;
};

#endif // GOTODIALOG_H
