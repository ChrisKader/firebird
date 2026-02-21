#ifndef ANSITEXTWRITER_H
#define ANSITEXTWRITER_H

#include <QObject>
#include <QTextCharFormat>

class QPlainTextEdit;

class AnsiTextWriter : public QObject
{
    Q_OBJECT
public:
    explicit AnsiTextWriter(QPlainTextEdit *target, QObject *parent = nullptr);
    void processChar(char c);
    void resetFormat();

private:
    QPlainTextEdit *m_target;
    enum EscapeState { None, Start, CSI };
    EscapeState m_escapeState = None;
    QByteArray m_escapeBuffer;
    char m_previous = 0;
    QTextCharFormat m_baseFormat;
    QTextCharFormat m_currentFormat;
    bool m_formatInitialized = false;

    void applySgr(const QList<int> &params);
};

#endif // ANSITEXTWRITER_H
