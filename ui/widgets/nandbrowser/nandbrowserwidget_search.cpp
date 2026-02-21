#include "nandbrowserwidget.h"

#include <algorithm>
#include <cstring>

#include "app/emuthread.h"
#include "core/storage/flash.h"
void NandBrowserWidget::onSearchTriggered()
{
    QString query = m_searchEdit->text();
    if (query.isEmpty()) return;

    const uint8_t *data = flash_get_nand_data();
    size_t nand_size = flash_get_nand_size();
    if (!data || nand_size == 0) return;

    m_infoLabel->setText(tr("Searching..."));

    EmuThread *emu = emuThreadInstance();
    if (!emu)
    {
        m_infoLabel->setText(tr("Emulator thread unavailable"));
        return;
    }

    // Pause emulator for consistent reads
    bool needsPause = emu->isRunning() && !emu->isPaused();
    if (needsPause)
        emu->setPaused(true);

    QByteArray needle = query.toUtf8();
    int needleLen = needle.size();

    // Determine search range
    size_t searchStart = 0, searchEnd = nand_size;
    int scopeIdx = m_searchScope->currentIndex();
    if (scopeIdx > 0 && scopeIdx - 1 < (int)m_partitions.size())
    {
        searchStart = m_partitions[scopeIdx - 1].offset;
        searchEnd = searchStart + m_partitions[scopeIdx - 1].size;
    }

    m_searchResults->setRowCount(0);
    m_searchResults->show();

    int maxResults = 500;
    int row = 0;

    for (size_t i = searchStart; i + needleLen <= searchEnd && row < maxResults; i++)
    {
        if (memcmp(data + i, needle.constData(), needleLen) == 0)
        {
            m_searchResults->setRowCount(row + 1);

            auto *offItem = new QTableWidgetItem(formatOffset(i));
            offItem->setData(Qt::UserRole, QVariant::fromValue((qulonglong)i));

            // Find which partition
            QString partName = tr("Unknown");
            for (size_t p = 0; p < m_partitions.size(); p++)
            {
                size_t pStart = m_partitions[p].offset;
                size_t pEnd = pStart + m_partitions[p].size;
                if (i >= pStart && i < pEnd)
                {
                    partName = QString::fromUtf8(m_partitions[p].name);
                    break;
                }
            }

            // Context: show surrounding bytes as ASCII
            size_t ctxStart = (i >= 16) ? i - 16 : 0;
            size_t ctxEnd = std::min(i + needleLen + 32, searchEnd);
            QString ctx;
            for (size_t j = ctxStart; j < ctxEnd; j++)
            {
                char c = data[j];
                ctx += (c >= 0x20 && c < 0x7F) ? QChar(QLatin1Char(c)) : QChar(QLatin1Char('.'));
            }

            m_searchResults->setItem(row, 0, offItem);
            m_searchResults->setItem(row, 1, new QTableWidgetItem(partName));
            m_searchResults->setItem(row, 2, new QTableWidgetItem(ctx));
            row++;
        }
    }

    if (needsPause)
        emu->setPaused(false);

    m_searchResults->resizeColumnsToContents();
    m_infoLabel->setText(tr("Search: %1 results for \"%2\"%3")
                              .arg(row).arg(query)
                              .arg(row >= maxResults ? tr(" (limited)") : QString()));
}

void NandBrowserWidget::onSearchResultClicked(QTableWidgetItem *item)
{
    int row = item->row();
    auto *offItem = m_searchResults->item(row, 0);
    if (!offItem) return;

    size_t offset = offItem->data(Qt::UserRole).toULongLong();
    // Show hex view centered on the result
    size_t start = (offset >= 256) ? offset - 256 : 0;
    showHexView(start, 1024);
}
