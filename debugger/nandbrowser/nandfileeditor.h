#ifndef NANDFILEEDITOR_H
#define NANDFILEEDITOR_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <string>
#include <vector>

#include "core/nand_fs.h"

class NandFileEditor : public QDialog
{
    Q_OBJECT
public:
    NandFileEditor(const NandFilesystem &fs, const NandFsNode &node,
                   const std::vector<uint8_t> &content, QWidget *parent = nullptr);

signals:
    void savedToNand();

private slots:
    void saveToNand();
    void saveAs();
    void revert();

private:
    const NandFilesystem &m_fs;
    const NandFsNode &m_node;
    std::vector<uint8_t> m_originalContent;
    QPlainTextEdit *m_editor;
    QLabel *m_statusLabel;
};

#endif // NANDFILEEDITOR_H
