#include "nandfileeditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFont>
#include <QFile>

#include "core/emu.h"
#include "core/memory/flash.h"

NandFileEditor::NandFileEditor(const NandFilesystem &fs, const NandFsNode &node,
                               const std::vector<uint8_t> &content, QWidget *parent)
    : QDialog(parent), m_fs(fs), m_node(node), m_originalContent(content)
{
    setWindowTitle(tr("Edit: %1").arg(QString::fromStdString(node.full_path)));
    resize(700, 500);

    auto *layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(tr("File: %1 (%2 bytes)")
                                    .arg(QString::fromStdString(node.full_path))
                                    .arg(content.size()), this);
    layout->addWidget(m_statusLabel);

    m_editor = new QPlainTextEdit(this);
    QFont mono(QStringLiteral("Menlo, Consolas, monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    m_editor->setFont(mono);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setPlainText(QString::fromUtf8(
        reinterpret_cast<const char *>(content.data()), (int)content.size()));
    layout->addWidget(m_editor, 1);

    auto *btnLayout = new QHBoxLayout;
    auto *saveNandBtn = new QPushButton(tr("Save to NAND"), this);
    auto *saveAsBtn = new QPushButton(tr("Save As..."), this);
    auto *revertBtn = new QPushButton(tr("Revert"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);

    btnLayout->addWidget(saveNandBtn);
    btnLayout->addWidget(saveAsBtn);
    btnLayout->addWidget(revertBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(saveNandBtn, &QPushButton::clicked, this, &NandFileEditor::saveToNand);
    connect(saveAsBtn, &QPushButton::clicked, this, &NandFileEditor::saveAs);
    connect(revertBtn, &QPushButton::clicked, this, &NandFileEditor::revert);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void NandFileEditor::saveToNand()
{
    QByteArray text = m_editor->toPlainText().toUtf8();
    auto *nand_ptr = const_cast<uint8_t *>(flash_get_nand_data());
    if (!nand_ptr)
    {
        QMessageBox::warning(this, tr("Error"), tr("No NAND data available"));
        return;
    }

    size_t nand_size = flash_get_nand_size();
    bool ok = nand_fs_write_file(m_fs, m_node,
                                  reinterpret_cast<const uint8_t *>(text.constData()),
                                  text.size(), nand_ptr, nand_size);

    if (!ok)
    {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to write file to NAND.\n"
                                "The new content (%1 bytes) may exceed the allocated space.")
                                 .arg(text.size()));
        return;
    }

    // Mark physical NAND blocks as modified so flash_save_as() persists the changes.
    // Translate Reliance FS block numbers -> NAND logical blocks -> physical blocks.
    uint32_t nand_block_data = m_fs.data_per_page * m_fs.pages_per_block;
    uint32_t pages_per_blk = 1u << nand.metrics.log2_pages_per_block;

    auto mark_fs_block = [&](uint32_t fs_blk) {
        if (m_fs.block_size == 0 || nand_block_data == 0) return;
        // Compute range of NAND logical blocks covered by this Reliance block
        size_t byte_start = (size_t)fs_blk * m_fs.block_size;
        size_t byte_end = byte_start + m_fs.block_size;
        uint32_t first_nand_blk = (uint32_t)(byte_start / nand_block_data);
        uint32_t last_nand_blk = (uint32_t)((byte_end - 1) / nand_block_data);
        for (uint32_t lb = first_nand_blk; lb <= last_nand_blk; lb++)
        {
            if (lb < m_fs.logical_to_physical.size())
            {
                uint32_t phys = m_fs.logical_to_physical[lb];
                if (phys != UINT32_MAX)
                {
                    uint32_t abs_page = (uint32_t)(m_fs.partition_offset / m_fs.page_size)
                                         + phys * pages_per_blk;
                    uint32_t abs_block = abs_page >> nand.metrics.log2_pages_per_block;
                    if (abs_block < 2048)
                        nand.nand_block_modified[abs_block] = true;
                }
            }
        }
    };

    if (m_node.storage_mode == 0 && m_node.inode_block != 0)
    {
        mark_fs_block(m_node.inode_block);
    }
    else
    {
        for (uint32_t blk : m_node.data_blocks)
            mark_fs_block(blk);
    }

    m_statusLabel->setText(tr("Saved %1 bytes to NAND").arg(text.size()));
    emit savedToNand();
}

void NandFileEditor::saveAs()
{
    QString filename = QFileDialog::getSaveFileName(
        this, tr("Save File As"),
        QString::fromStdString(m_node.name));
    if (filename.isEmpty())
        return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }

    QByteArray text = m_editor->toPlainText().toUtf8();
    f.write(text);
    f.close();
    m_statusLabel->setText(tr("Saved to %1").arg(filename));
}

void NandFileEditor::revert()
{
    m_editor->setPlainText(QString::fromUtf8(
        reinterpret_cast<const char *>(m_originalContent.data()),
        (int)m_originalContent.size()));
    m_statusLabel->setText(tr("Reverted to original content"));
}
