#include "consolewidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QStringListModel>
#include <QTextCursor>
#include <QRegularExpression>

#include "ui/ansitextwriter.h"
#include "ui/widgettheme.h"

ConsoleWidget::ConsoleWidget(QWidget *parent)
    : QWidget(parent)
{
    m_elapsed.start();

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /* Quick commands toolbar */
    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));

    auto addQuick = [&](const QString &label, const QString &tip, const QString &cmd) {
        QAction *act = toolbar->addAction(label);
        act->setToolTip(tip);
        connect(act, &QAction::triggered, this, [this, cmd]() {
            emit commandSubmitted(cmd);
            appendOutput(QStringLiteral("> %1\n").arg(cmd));
        });
    };

    addQuick(QStringLiteral("\u25B6"), tr("Continue"), QStringLiteral("c"));
    addQuick(QStringLiteral("\u2193"), tr("Step"), QStringLiteral("s"));
    addQuick(QStringLiteral("\u2192"), tr("Step Over"), QStringLiteral("n"));
    addQuick(QStringLiteral("\u2191"), tr("Step Out"), QStringLiteral("finish"));
    toolbar->addSeparator();
    addQuick(QStringLiteral("Regs"), tr("Print Registers"), QStringLiteral("r"));

    layout->addWidget(toolbar);

    /* Output area */
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(5000);
    layout->addWidget(m_output, 1);

    /* ANSI text writer for UART output (supports escape sequences) */
    m_ansiWriter = new AnsiTextWriter(m_output, this);

    /* Input line */
    auto *inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(2, 2, 2, 2);
    inputLayout->setSpacing(4);

    auto *prompt = new QLabel(QStringLiteral("> "), this);
    prompt->setFont(mono);
    inputLayout->addWidget(prompt);

    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->setPlaceholderText(tr("debugger command..."));
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ConsoleWidget::onReturnPressed);
    inputLayout->addWidget(m_input, 1);

    layout->addLayout(inputLayout);

    /* Tab completion for debugger commands */
    QStringList commands = {
        QStringLiteral("c"), QStringLiteral("s"), QStringLiteral("n"),
        QStringLiteral("finish"), QStringLiteral("r"),
        QStringLiteral("pr"), QStringLiteral("pw"),
        QStringLiteral("d"), QStringLiteral("db"),
        QStringLiteral("m"), QStringLiteral("bt"),
        QStringLiteral("wm"), QStringLiteral("wf"),
        QStringLiteral("ss"), QStringLiteral("sr"),
        QStringLiteral("j"),
    };

    m_completer = new QCompleter(commands, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::InlineCompletion);
    m_input->setCompleter(m_completer);
}

/* -- Formatting helpers --------------------------------------- */

void ConsoleWidget::insertTimestamp()
{
    const WidgetTheme &t = currentWidgetTheme();
    qint64 ms = m_elapsed.elapsed();
    int secs = (int)(ms / 1000);
    int msec = (int)(ms % 1000);
    QString ts = QStringLiteral("[%1:%2.%3] ")
        .arg(secs / 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'))
        .arg(msec, 3, 10, QLatin1Char('0'));

    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(t.textMuted);
    cursor.insertText(ts, fmt);
    m_output->setTextCursor(cursor);
}

static QColor tagColor(ConsoleTag tag)
{
    const WidgetTheme &t = currentWidgetTheme();
    switch (tag) {
    case ConsoleTag::Uart:  return t.consoleTagUart;
    case ConsoleTag::Debug: return t.consoleTagDebug;
    case ConsoleTag::Sys:   return t.consoleTagSys;
    }
    return t.textMuted;
}

static QString tagLabel(ConsoleTag tag)
{
    switch (tag) {
    case ConsoleTag::Uart:  return QStringLiteral("[UART] ");
    case ConsoleTag::Debug: return QStringLiteral("[DBG]  ");
    case ConsoleTag::Sys:   return QStringLiteral("[SYS]  ");
    }
    return QStringLiteral("[???]  ");
}

void ConsoleWidget::insertTag(ConsoleTag tag)
{
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(tagColor(tag));
    fmt.setFontWeight(QFont::Bold);
    cursor.insertText(tagLabel(tag), fmt);
    m_output->setTextCursor(cursor);
}

void ConsoleWidget::insertStyledText(const QString &text, const QColor &color)
{
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    cursor.insertText(text, fmt);
    m_output->setTextCursor(cursor);
}

void ConsoleWidget::insertAnsiText(const QString &text)
{
    /* Reset ANSI writer to default text color so previous formatting
     * doesn't leak into this line's body. */
    const WidgetTheme &t = currentWidgetTheme();
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(t.text);
    cursor.setCharFormat(fmt);
    m_output->setTextCursor(cursor);
    m_ansiWriter->resetFormat();

    for (int i = 0; i < text.size(); i++)
        m_ansiWriter->processChar(text.at(i).toLatin1());
}

void ConsoleWidget::insertDebugFormattedText(const QString &text)
{
    /* Syntax-highlight debug output to match the disassembly theme:
     *   - 0x-prefixed hex  -> syntaxAddress (gray)
     *   - register names   -> syntaxRegister (teal)
     *   - hex after '='    -> syntaxImmediate (green)
     *   - 8-char hex words -> syntaxImmediate (green)
     *   - everything else  -> default text color */
    const WidgetTheme &t = currentWidgetTheme();
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat defFmt, addrFmt, regFmt, numFmt;
    defFmt.setForeground(t.text);
    addrFmt.setForeground(t.syntaxAddress);
    regFmt.setForeground(t.syntaxRegister);
    numFmt.setForeground(t.syntaxImmediate);

    static const QRegularExpression re(
        QStringLiteral("(0x[0-9A-Fa-f]+)"                          // group 1: 0x-prefixed hex
                       "|\\b(r[0-9]{1,2}|sp|lr|pc|cpsr|spsr)\\b"   // group 2: register name
                       "|(?<=\\=)([0-9A-Fa-f]{2,8})"               // group 3: hex value after '='
                       "|(\\b[0-9A-Fa-f]{8}\\b)")                   // group 4: standalone 8-char hex
    );

    int pos = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();

        /* Text before this match: default format */
        if (match.capturedStart() > pos)
            cursor.insertText(text.mid(pos, match.capturedStart() - pos), defFmt);

        /* Classify and color the match */
        if (!match.captured(1).isEmpty())
            cursor.insertText(match.captured(), addrFmt);    // 0x-prefixed -> address
        else if (!match.captured(2).isEmpty())
            cursor.insertText(match.captured(), regFmt);     // register name
        else if (!match.captured(3).isEmpty())
            cursor.insertText(match.captured(), numFmt);     // hex after '='
        else if (!match.captured(4).isEmpty())
            cursor.insertText(match.captured(), numFmt);     // 8-char hex value

        pos = match.capturedEnd();
    }

    /* Remaining text after last match */
    if (pos < text.size())
        cursor.insertText(text.mid(pos), defFmt);

    m_output->setTextCursor(cursor);
}

/* -- Public output methods ------------------------------------ */

void ConsoleWidget::appendOutput(const QString &text)
{
    /* Plain text output (user commands, raw messages).
     * Timestamped, solid default text color -- no tag. */
    const WidgetTheme &t = currentWidgetTheme();
    int pos = 0;
    while (pos <= text.size()) {
        int nl = text.indexOf(QLatin1Char('\n'), pos);
        if (nl < 0) nl = text.size();

        QString segment = text.mid(pos, nl - pos);

        if (!segment.isEmpty()) {
            if (m_atLineStart) {
                insertTimestamp();
                m_atLineStart = false;
            }
            insertStyledText(segment, t.text);
        }

        if (nl < text.size()) {
            insertStyledText(QStringLiteral("\n"), t.text);
            m_atLineStart = true;
        }

        pos = nl + 1;
    }

    m_output->moveCursor(QTextCursor::End);
}

void ConsoleWidget::appendTaggedOutput(ConsoleTag tag, const QString &text)
{
    /* Each line gets: [MM:SS.mmm] [TAG] <body>
     * Body formatting depends on the source tag. */
    const WidgetTheme &t = currentWidgetTheme();
    int pos = 0;
    while (pos <= text.size()) {
        int nl = text.indexOf(QLatin1Char('\n'), pos);
        if (nl < 0) nl = text.size();

        QString line = text.mid(pos, nl - pos);

        if (!line.isEmpty()) {
            insertTimestamp();
            insertTag(tag);

            switch (tag) {
            case ConsoleTag::Uart:
                insertAnsiText(line);
                break;
            case ConsoleTag::Debug:
                insertDebugFormattedText(line);
                break;
            case ConsoleTag::Sys:
                insertStyledText(line, t.text);
                break;
            }
        }

        if (nl < text.size())
            insertStyledText(QStringLiteral("\n"), t.text);

        pos = nl + 1;
    }

    m_output->moveCursor(QTextCursor::End);
}

/* -- Input handling ------------------------------------------- */

void ConsoleWidget::focusInput()
{
    if (m_input)
        m_input->setFocus();
}

void ConsoleWidget::onReturnPressed()
{
    QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty())
        return;

    /* Add to history */
    if (m_cmdHistory.isEmpty() || m_cmdHistory.last() != cmd) {
        m_cmdHistory.append(cmd);
        if (m_cmdHistory.size() > MAX_HISTORY)
            m_cmdHistory.removeFirst();
    }
    m_historyIdx = -1;

    appendOutput(QStringLiteral("> %1\n").arg(cmd));
    m_input->clear();

    emit commandSubmitted(cmd);
}

bool ConsoleWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);

        if (ke->key() == Qt::Key_Up) {
            if (m_cmdHistory.isEmpty())
                return true;
            if (m_historyIdx < 0)
                m_historyIdx = m_cmdHistory.size() - 1;
            else if (m_historyIdx > 0)
                m_historyIdx--;
            m_input->setText(m_cmdHistory.at(m_historyIdx));
            return true;
        }

        if (ke->key() == Qt::Key_Down) {
            if (m_historyIdx < 0)
                return true;
            m_historyIdx++;
            if (m_historyIdx >= m_cmdHistory.size()) {
                m_historyIdx = -1;
                m_input->clear();
            } else {
                m_input->setText(m_cmdHistory.at(m_historyIdx));
            }
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}
