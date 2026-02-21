#include "nandbrowserwidget.h"

#include "nandfileeditor.h"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>

#include "core/memory/flash.h"
#include "core/memory/nand_fs.h"
void NandBrowserWidget::exportPartition(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    QString name = QString::fromUtf8(m_partitions[partIndex].name);
    QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Partition"),
        name + QStringLiteral(".bin"),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }

    f.write(reinterpret_cast<const char *>(data + m_partitions[partIndex].offset),
            m_partitions[partIndex].size);
    f.close();

    m_infoLabel->setText(tr("Exported %1 (%2) to %3")
                              .arg(name, formatSize(m_partitions[partIndex].size), filename));
}

void NandBrowserWidget::importPartition(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    QString filename = QFileDialog::getOpenFileName(
        this, tr("Import Partition"),
        QString(),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file"));
        return;
    }

    QByteArray content = f.readAll();
    f.close();

    if ((size_t)content.size() > m_partitions[partIndex].size)
    {
        auto ret = QMessageBox::warning(this, tr("Size Mismatch"),
                                         tr("File is %1 but partition is only %2. Truncate?")
                                             .arg(formatSize(content.size()),
                                                  formatSize(m_partitions[partIndex].size)),
                                         QMessageBox::Yes | QMessageBox::Cancel);
        if (ret != QMessageBox::Yes) return;
        content.truncate(m_partitions[partIndex].size);
    }

    bool ok = flash_write_raw(m_partitions[partIndex].offset,
                               reinterpret_cast<const uint8_t *>(content.constData()),
                               content.size());
    if (ok)
        m_infoLabel->setText(tr("Imported %1 bytes into %2")
                                  .arg(content.size())
                                  .arg(QString::fromUtf8(m_partitions[partIndex].name)));
    else
        QMessageBox::warning(this, tr("Error"), tr("Failed to write to NAND"));
}

void NandBrowserWidget::exportPage(size_t offset, size_t size)
{
    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Page"),
        QStringLiteral("page_%1.bin").arg(offset, 8, 16, QLatin1Char('0')),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(reinterpret_cast<const char *>(data + offset), size);
    f.close();
}

// -------------------- Filesystem file operations --------------------

void NandBrowserWidget::extractFile(const NandFsNode *node)
{
    if (!node || !m_fsValid) return;

    auto data = nand_fs_read_file(*m_filesystem, *node,
                                   flash_get_nand_data(), flash_get_nand_size());

    QString filename = QFileDialog::getSaveFileName(
        this, tr("Extract File"),
        QString::fromStdString(node->name));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }
    f.write(reinterpret_cast<const char *>(data.data()), data.size());
    f.close();

    m_infoLabel->setText(tr("Extracted %1 (%2 bytes)")
                              .arg(QString::fromStdString(node->name)).arg(data.size()));
}

void NandBrowserWidget::editFile(const NandFsNode *node)
{
    if (!node || !m_fsValid) return;

    auto data = nand_fs_read_file(*m_filesystem, *node,
                                   flash_get_nand_data(), flash_get_nand_size());

    auto *editor = new NandFileEditor(*m_filesystem, *node, data, this);
    connect(editor, &NandFileEditor::savedToNand, this, [this]() {
        m_infoLabel->setText(tr("File saved to NAND. Use Flash > Save Changes to persist."));
    });
    editor->exec();
    delete editor;
}
