#include "hexviewwidget.h"

#include "core/debug/debug_api.h"

QByteArray HexViewWidget::buildSearchPattern() const
{
    QString text = m_searchEdit->text();
    if (text.isEmpty())
        return {};

    int type = m_searchTypeCombo->currentData().toInt();
    QByteArray pattern;

    switch (type) {
    case SearchHex: {
        QString hexStr = text.remove(QLatin1Char(' '));
        for (int i = 0; i + 1 < hexStr.size(); i += 2) {
            bool ok;
            uint8_t byte = hexStr.mid(i, 2).toUInt(&ok, 16);
            if (!ok) return {};
            pattern.append(static_cast<char>(byte));
        }
        break;
    }
    case SearchAscii:
        pattern = text.toLatin1();
        break;
    case SearchUint32LE: {
        bool ok;
        uint32_t val = text.toUInt(&ok, 16);
        if (!ok) return {};
        pattern.resize(4);
        pattern[0] = static_cast<char>(val & 0xFF);
        pattern[1] = static_cast<char>((val >> 8) & 0xFF);
        pattern[2] = static_cast<char>((val >> 16) & 0xFF);
        pattern[3] = static_cast<char>((val >> 24) & 0xFF);
        break;
    }
    case SearchUint32BE: {
        bool ok;
        uint32_t val = text.toUInt(&ok, 16);
        if (!ok) return {};
        pattern.resize(4);
        pattern[0] = static_cast<char>((val >> 24) & 0xFF);
        pattern[1] = static_cast<char>((val >> 16) & 0xFF);
        pattern[2] = static_cast<char>((val >> 8) & 0xFF);
        pattern[3] = static_cast<char>(val & 0xFF);
        break;
    }
    }

    return pattern;
}

void HexViewWidget::doSearch(bool forward)
{
    QByteArray pattern = buildSearchPattern();
    if (pattern.isEmpty())
        return;

    uint32_t start;
    if (forward)
        start = selectedAddress() + 1;
    else
        start = (selectedAddress() > 1) ? selectedAddress() - 1 : 0;

    /* For backward search, we search from 0 to current position and take the last match */
    if (!forward) {
        uint32_t lastMatch = 0xFFFFFFFF;
        uint32_t pos = 0;
        while (pos < start) {
            uint32_t result = debug_search_memory(pos, start - pos,
                                                   reinterpret_cast<const uint8_t *>(pattern.constData()),
                                                   pattern.size());
            if (result == 0xFFFFFFFF)
                break;
            lastMatch = result;
            pos = result + 1;
        }
        if (lastMatch != 0xFFFFFFFF)
            goToAddress(lastMatch);
        return;
    }

    uint32_t result = debug_search_memory(start, 0x1000000,
                                           reinterpret_cast<const uint8_t *>(pattern.constData()),
                                           pattern.size());
    if (result != 0xFFFFFFFF) {
        goToAddress(result);
    }
}

void HexViewWidget::doFindAll()
{
    QByteArray pattern = buildSearchPattern();
    if (pattern.isEmpty())
        return;

    m_findResultsList->clear();
    m_findResultsList->setVisible(true);

    uint32_t pos = 0;
    int maxResults = 1000;
    while (maxResults-- > 0) {
        uint32_t result = debug_search_memory(pos, 0x10000000,
                                               reinterpret_cast<const uint8_t *>(pattern.constData()),
                                               pattern.size());
        if (result == 0xFFFFFFFF)
            break;

        auto *item = new QListWidgetItem(
            QStringLiteral("0x%1").arg(result, 8, 16, QLatin1Char('0')),
            m_findResultsList);
        item->setData(Qt::UserRole, result);

        pos = result + 1;
    }

    if (m_findResultsList->count() == 0)
        m_findResultsList->addItem(tr("No matches found"));
}
