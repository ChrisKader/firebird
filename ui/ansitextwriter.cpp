#include "ansitextwriter.h"
#include "ui/widgettheme.h"

#include <QPlainTextEdit>
#include <QTextCursor>

AnsiTextWriter::AnsiTextWriter(QPlainTextEdit *target, QObject *parent)
    : QObject(parent), m_target(target)
{
}

void AnsiTextWriter::resetFormat()
{
    m_baseFormat = m_target->currentCharFormat();
    m_currentFormat = m_baseFormat;
    m_formatInitialized = true;
}

void AnsiTextWriter::applySgr(const QList<int> &params)
{
    if (params.isEmpty())
    {
        m_currentFormat = m_baseFormat;
        return;
    }

    for (int code : params)
    {
        if (code == 0)
        {
            m_currentFormat = m_baseFormat;
        }
        else if (code == 1)
        {
            m_currentFormat.setFontWeight(QFont::Bold);
        }
        else if (code == 22)
        {
            m_currentFormat.setFontWeight(m_baseFormat.fontWeight());
        }
        else if (code == 39)
        {
            m_currentFormat.setForeground(m_baseFormat.foreground());
        }
        else if (code >= 30 && code <= 37)
        {
            const WidgetTheme &t = currentWidgetTheme();
            const QColor colors[] = {
                t.ansiBlack, Qt::red, Qt::green, t.ansiYellow,
                Qt::blue, Qt::magenta, Qt::cyan, Qt::lightGray};
            m_currentFormat.setForeground(colors[code - 30]);
        }
        else if (code >= 90 && code <= 97)
        {
            const WidgetTheme &t = currentWidgetTheme();
            const QColor bright_colors[] = {
                Qt::darkGray, Qt::red, Qt::green, t.ansiYellow,
                Qt::blue, Qt::magenta, Qt::cyan, Qt::white};
            m_currentFormat.setForeground(bright_colors[code - 90]);
        }
    }
}

void AnsiTextWriter::processChar(char c)
{
    m_target->moveCursor(QTextCursor::End);

    if (!m_formatInitialized)
    {
        m_baseFormat = m_target->currentCharFormat();
        m_currentFormat = m_baseFormat;
        m_formatInitialized = true;
    }

    if (m_escapeState == Start)
    {
        if (c == '[')
        {
            m_escapeState = CSI;
            m_escapeBuffer.clear();
        }
        else if (c >= 0x40 && c <= 0x7E)
        {
            m_escapeState = None;
        }
        else
        {
            m_escapeState = None;
        }
        m_previous = 0;
        return;
    }
    if (m_escapeState == CSI)
    {
        if (c >= 0x40 && c <= 0x7E)
        {
            if (c == 'm')
            {
                QList<int> params;
                if (m_escapeBuffer.isEmpty())
                    params.append(0);
                else
                {
                    for (const QByteArray &part : m_escapeBuffer.split(';'))
                        params.append(part.isEmpty() ? 0 : part.toInt());
                }
                applySgr(params);
            }
            m_escapeState = None;
            m_escapeBuffer.clear();
            m_previous = 0;
            return;
        }
        m_escapeBuffer.append(c);
        return;
    }
    if (c == '\x1B')
    {
        m_escapeState = Start;
        m_previous = 0;
        return;
    }

    switch (c)
    {
    case 0:
    case '\r':
        m_previous = c;
        break;

    case '\b':
        m_target->textCursor().deletePreviousChar();
        break;

    default:
        if (m_previous == '\r' && c != '\n')
        {
            m_target->moveCursor(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
            m_target->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
            m_target->textCursor().removeSelectedText();
            m_previous = 0;
        }
        {
            QTextCursor cursor = m_target->textCursor();
            cursor.insertText(QString(QChar::fromLatin1(c)), m_currentFormat);
            m_target->setTextCursor(cursor);
        }
    }
}
