#include "disassemblywidget.h"

#include <QFile>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextStream>

#include <cstring>

#include "core/debug_api.h"

/* -- Syntax helpers ------------------------------------------------------- */

void DisassemblyWidget::parseMnemonicOperands(const QString &text,
                                               QString &mnemonic, QString &operands)
{
    int tab = text.indexOf(QLatin1Char('\t'));
    if (tab < 0) {
        mnemonic = text.trimmed();
        operands.clear();
    } else {
        mnemonic = text.left(tab).trimmed();
        operands = text.mid(tab + 1).trimmed();
    }
}

bool DisassemblyWidget::isBranchMnemonic(const QString &mnem) const
{
    if (mnem.startsWith(QLatin1String("b"), Qt::CaseInsensitive)) {
        if (mnem.size() == 1) return true;
        QChar c = mnem.at(1);
        if (c == QLatin1Char('l') || c == QLatin1Char('x') || c == QLatin1Char('e')
            || c == QLatin1Char('n') || c == QLatin1Char('c') || c == QLatin1Char('m')
            || c == QLatin1Char('p') || c == QLatin1Char('v') || c == QLatin1Char('h')
            || c == QLatin1Char('g') || c == QLatin1Char('l'))
            return true;
        if (mnem == QLatin1String("bic")) return false;
    }
    if (mnem == QLatin1String("swi") || mnem == QLatin1String("svc"))
        return true;
    return false;
}

uint32_t DisassemblyWidget::parseBranchTarget(const Line &line) const
{
    /* Try to extract hex address from operands like "0x10001234" */
    QString ops = line.operands.trimmed();
    if (ops.startsWith(QLatin1String("0x")) || ops.startsWith(QLatin1String("0X"))) {
        bool ok;
        uint32_t addr = ops.mid(2).split(QLatin1Char(',')).first().toUInt(&ok, 16);
        if (ok) return addr;
    }
    /* Try bare hex */
    bool ok;
    uint32_t addr = ops.split(QLatin1Char(',')).first().toUInt(&ok, 16);
    if (ok && addr >= 0x10000) return addr;

    return 0xFFFFFFFF;
}

QString DisassemblyWidget::symbolForAddress(uint32_t addr) const
{
    auto it = m_symbols.constFind(addr);
    return (it != m_symbols.constEnd()) ? it.value() : QString();
}

/* -- Symbol file loading -------------------------------------------------- */

bool DisassemblyWidget::loadSymbolFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_symbols.clear();
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        /* Support formats: "addr=name" or "addr name" */
        int sep = line.indexOf(QLatin1Char('='));
        if (sep < 0) sep = line.indexOf(QLatin1Char(' '));
        if (sep < 0) continue;

        bool ok;
        uint32_t addr = line.left(sep).trimmed().toUInt(&ok, 16);
        if (!ok) continue;

        QString name = line.mid(sep + 1).trimmed();
        if (!name.isEmpty())
            m_symbols.insert(addr, name);
    }

    return true;
}

/* -- Navigation history --------------------------------------------------- */

void DisassemblyWidget::pushNavHistory(uint32_t addr)
{
    m_navHistory.push(addr);
    m_navForward.clear();
}

void DisassemblyWidget::navBack()
{
    if (m_navHistory.isEmpty()) return;
    m_navForward.push(m_baseAddr);
    uint32_t addr = m_navHistory.pop();
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

void DisassemblyWidget::navForward()
{
    if (m_navForward.isEmpty()) return;
    m_navHistory.push(m_baseAddr);
    uint32_t addr = m_navForward.pop();
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

void DisassemblyWidget::refresh()
{
    /* CPU execution may have changed code and PC; invalidate cached disassembly
     * window so the next update re-queries core state. */
    m_cachedWindowValid = false;

    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;
    debug_get_registers(regs, &cpsr, &spsr, &has_spsr);
    m_pcAddr = regs[15];

    goToPC();
}

void DisassemblyWidget::goToPC()
{
    int visible = visibleLineCount();
    int before = visible / 3;

    bool is_thumb = debug_is_thumb_mode();
    uint32_t step = is_thumb ? 2 : 4;
    m_baseAddr = m_pcAddr - before * step;

    updateLines();
    viewport()->update();
}

void DisassemblyWidget::goToAddress(uint32_t addr)
{
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

QJsonObject DisassemblyWidget::serializeState() const
{
    QJsonObject state;
    state.insert(QStringLiteral("baseAddr"), QStringLiteral("%1").arg(m_baseAddr, 8, 16, QLatin1Char('0')));
    if (m_searchEdit)
        state.insert(QStringLiteral("searchText"), m_searchEdit->text());
    return state;
}

void DisassemblyWidget::restoreState(const QJsonObject &state)
{
    if (m_searchEdit)
        m_searchEdit->setText(state.value(QStringLiteral("searchText")).toString());

    bool ok = false;
    uint32_t addr = state.value(QStringLiteral("baseAddr")).toString().toUInt(&ok, 16);
    if (!ok) {
        const int fallback = state.value(QStringLiteral("baseAddr")).toInt(-1);
        if (fallback >= 0) {
            addr = static_cast<uint32_t>(fallback);
            ok = true;
        }
    }
    if (ok)
        goToAddress(addr);
}

void DisassemblyWidget::updateLines()
{
    int count = 0;
    const debug_disasm_line *raw_lines = nullptr;

    if (m_cachedWindowValid && m_cachedBaseAddr == m_baseAddr) {
        count = m_cachedWindow.size();
        raw_lines = m_cachedWindow.constData();
    } else {
        struct debug_disasm_line window[NUM_LINES];
        count = debug_disassemble(m_baseAddr, window, NUM_LINES);
        if (count < 0)
            count = 0;

        m_cachedWindow.resize(count);
        if (count > 0)
            std::memcpy(m_cachedWindow.data(), window, sizeof(debug_disasm_line) * static_cast<size_t>(count));
        m_cachedBaseAddr = m_baseAddr;
        m_cachedWindowValid = true;
        raw_lines = m_cachedWindow.constData();
    }

    struct debug_breakpoint bps[256];
    int bp_count = debug_list_breakpoints(bps, 256);

    m_lines.resize(count);
    for (int i = 0; i < count; i++) {
        Line &line = m_lines[i];
        line.addr = raw_lines[i].addr;
        line.raw = raw_lines[i].raw;
        line.size = raw_lines[i].size;
        line.is_thumb = raw_lines[i].is_thumb;
        line.is_pc = (line.addr == m_pcAddr);
        line.has_exec_bp = false;
        line.has_read_wp = false;
        line.has_write_wp = false;

        QString full = QString::fromLatin1(raw_lines[i].text);
        parseMnemonicOperands(full, line.mnemonic, line.operands);

        for (int b = 0; b < bp_count; b++) {
            if (bps[b].addr == line.addr) {
                if (bps[b].exec) line.has_exec_bp = true;
                if (bps[b].read) line.has_read_wp = true;
                if (bps[b].write) line.has_write_wp = true;
            }
        }
    }

    updateScrollBar();
}

void DisassemblyWidget::updateScrollBar()
{
    verticalScrollBar()->setRange(0, qMax(0, m_lines.size() - visibleLineCount()));
    verticalScrollBar()->setPageStep(visibleLineCount());
}

void DisassemblyWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    if (m_toolbar) {
        int h = m_toolbar->sizeHint().height();
        m_toolbar->parentWidget()->setGeometry(0, 0, width(), h);
        setViewportMargins(0, h, 0, 0);
    }

    updateScrollBar();
}
