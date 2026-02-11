#ifndef CONSOLEWIDGET_H
#define CONSOLEWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QStringList>
#include <QCompleter>
#include <QElapsedTimer>

#include "ui/dockstate.h"

class AnsiTextWriter;

/* Source tag for console output lines */
enum class ConsoleTag { Debug, Uart, Sys, Nlog };

class ConsoleWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit ConsoleWidget(QWidget *parent = nullptr);
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

public slots:
    /** Append plain text (user commands, raw output). Timestamped, default text color. */
    void appendOutput(const QString &text);

    /** Append tagged output from a specific source.
     *  Each line gets: [timestamp] [TAG] <body>
     *  - Uart:  body passes through ANSI escape processor
     *  - Debug: body is syntax-highlighted unless ANSI escapes are present
     *  - Sys/Nlog: body uses ANSI when present, otherwise default text color */
    void appendTaggedOutput(ConsoleTag tag, const QString &text);

    void focusInput();

signals:
    void commandSubmitted(const QString &cmd);

private slots:
    void onReturnPressed();
    void clearConsoleOutput();
    void showOutputContextMenu(const QPoint &pos);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    bool lineMatchesFilter(const QString &line) const;
    void insertTimestamp();
    void insertTag(ConsoleTag tag);
    void insertDebugFormattedText(const QString &text);
    void insertAnsiText(const QString &text);
    void insertStyledText(const QString &text, const QColor &color);

    QPlainTextEdit *m_output = nullptr;
    AnsiTextWriter *m_ansiWriter = nullptr;
    QLineEdit *m_input = nullptr;
    QLineEdit *m_filterInput = nullptr;
    QCompleter *m_completer = nullptr;

    QStringList m_cmdHistory;
    int m_historyIdx = -1;
    static constexpr int MAX_HISTORY = 100;

    QElapsedTimer m_elapsed;
    QString m_filterText;
    bool m_atLineStart = true;
    bool m_taggedAtLineStart = true;
    bool m_hasActiveTaggedTag = false;
    ConsoleTag m_activeTaggedTag = ConsoleTag::Sys;
};

#endif // CONSOLEWIDGET_H
