#include "nandbrowserwidget.h"

#include <algorithm>

#include "core/flash.h"
void NandBrowserWidget::showPartitionPages(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    size_t offset = m_partitions[partIndex].offset;
    size_t size = m_partitions[partIndex].size;
    uint32_t pageSize = nand.metrics.page_size;
    uint32_t pagesPerBlock = 1u << nand.metrics.log2_pages_per_block;

    m_pageTable->setRowCount(0);
    uint32_t numPages = (uint32_t)(size / pageSize);
    // Limit display to first 1024 pages for performance
    uint32_t displayPages = std::min(numPages, 1024u);

    m_pageTable->setRowCount(displayPages);
    size_t nand_size = flash_get_nand_size();

    for (uint32_t i = 0; i < displayPages; i++)
    {
        size_t pageOff = offset + (size_t)i * pageSize;
        uint32_t block = i / pagesPerBlock;

        auto *pageItem = new QTableWidgetItem(QString::number(i));
        auto *blockItem = new QTableWidgetItem(QString::number(block));
        auto *offsetItem = new QTableWidgetItem(formatOffset(pageOff));
        offsetItem->setData(Qt::UserRole, QVariant::fromValue((qulonglong)pageOff));

        // Check if page is erased
        bool erased = true;
        if (pageOff + 16 <= nand_size)
        {
            for (int b = 0; b < 16; b++)
            {
                if (data[pageOff + b] != 0xFF) { erased = false; break; }
            }
        }

        auto *statusItem = new QTableWidgetItem(erased ? tr("Erased") : tr("Data"));
        if (erased)
            statusItem->setForeground(Qt::gray);

        // Preview: first 16 bytes as hex
        QString preview;
        if (pageOff + 16 <= nand_size)
        {
            for (int b = 0; b < 16; b++)
                preview += QStringLiteral("%1 ").arg(data[pageOff + b], 2, 16, QLatin1Char('0'));
        }
        auto *previewItem = new QTableWidgetItem(preview.trimmed());
        if (erased)
            previewItem->setForeground(Qt::gray);

        m_pageTable->setItem(i, 0, pageItem);
        m_pageTable->setItem(i, 1, blockItem);
        m_pageTable->setItem(i, 2, offsetItem);
        m_pageTable->setItem(i, 3, statusItem);
        m_pageTable->setItem(i, 4, previewItem);
    }

    if (numPages > displayPages)
    {
        m_infoLabel->setText(tr("Showing first %1 of %2 pages for %3")
                                  .arg(displayPages).arg(numPages)
                                  .arg(QString::fromUtf8(m_partitions[partIndex].name)));
    }

    m_pageTable->resizeColumnsToContents();
    m_rightPane->setCurrentWidget(m_pageTable);
}

// -------------------- Hex view --------------------

void NandBrowserWidget::showHexView(size_t offset, size_t size)
{
    const uint8_t *data = flash_get_nand_data();
    size_t nand_size = flash_get_nand_size();
    if (!data || offset >= nand_size)
    {
        m_hexView->setPlainText(tr("(No data)"));
        m_rightPane->setCurrentWidget(m_hexView);
        return;
    }

    if (offset + size > nand_size)
        size = nand_size - offset;

    // Limit to 64KB for display
    size_t displaySize = std::min(size, (size_t)(64 * 1024));

    QString hex;
    hex.reserve((int)(displaySize / 16 + 1) * 80);

    for (size_t i = 0; i < displaySize; i += 16)
    {
        hex += QStringLiteral("%1: ").arg(offset + i, 8, 16, QLatin1Char('0'));

        for (size_t j = 0; j < 16; j++)
        {
            if (i + j < displaySize)
                hex += QStringLiteral("%1 ").arg(data[offset + i + j], 2, 16, QLatin1Char('0'));
            else
                hex += QStringLiteral("   ");
            if (j == 7) hex += QLatin1Char(' ');
        }

        hex += QStringLiteral(" |");
        for (size_t j = 0; j < 16 && i + j < displaySize; j++)
        {
            char c = data[offset + i + j];
            hex += (c >= 0x20 && c < 0x7F) ? QChar(QLatin1Char(c)) : QChar(QLatin1Char('.'));
        }
        hex += QStringLiteral("|\n");
    }

    if (displaySize < size)
        hex += QStringLiteral("\n... (%1 more bytes)\n").arg(size - displaySize);

    m_hexView->setPlainText(hex);
    m_rightPane->setCurrentWidget(m_hexView);
}

// -------------------- Text preview --------------------

void NandBrowserWidget::showTextPreview(const std::vector<uint8_t> &data, const QString &title)
{
    QString text = QString::fromUtf8(reinterpret_cast<const char *>(data.data()), (int)data.size());
    m_textPreview->setPlainText(text);
    m_rightPane->setCurrentWidget(m_textPreview);

    // Also update info
    m_infoLabel->setText(tr("Preview: %1 (%2 bytes)").arg(title).arg(data.size()));
}
