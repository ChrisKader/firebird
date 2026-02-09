#ifndef CONSOLEWIDGET_H
#define CONSOLEWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QStringList>
#include <QCompleter>
#include <QElapsedTimer>

class AnsiTextWriter;

/* Source tag for console output lines */
enum class ConsoleTag { Debug, Uart, Sys };

class ConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ConsoleWidget(QWidget *parent = nullptr);

public slots:
    /** Append plain text (user commands, raw output). Timestamped, default text color. */
    void appendOutput(const QString &text);

    /** Append tagged output from a specific source.
     *  Each line gets: [timestamp] [TAG] <body>
     *  - Uart:  body passes through ANSI escape processor
     *  - Debug: body is syntax-highlighted (addresses, registers, hex values)
     *  - Sys:   body in default text color */
    void appendTaggedOutput(ConsoleTag tag, const QString &text);

    void focusInput();

signals:
    void commandSubmitted(const QString &cmd);

private slots:
    void onReturnPressed();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void insertTimestamp();
    void insertTag(ConsoleTag tag);
    void insertDebugFormattedText(const QString &text);
    void insertAnsiText(const QString &text);
    void insertStyledText(const QString &text, const QColor &color);

    QPlainTextEdit *m_output = nullptr;
    AnsiTextWriter *m_ansiWriter = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;

    QStringList m_cmdHistory;
    int m_historyIdx = -1;
    static constexpr int MAX_HISTORY = 100;

    QElapsedTimer m_elapsed;
    bool m_atLineStart = true;
};

#endif // CONSOLEWIDGET_H
