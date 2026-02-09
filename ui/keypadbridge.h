#ifndef QTKEYPADBRIDGE_H
#define QTKEYPADBRIDGE_H

#include <QKeyEvent>

/* This class is used by every Widget which wants to interact with the
 * virtual keypad. Simply call QtKeypadBridge::keyPressEvent or keyReleaseEvent
 * to relay the key events into the virtual calc. */

class QtKeypadBridge : public QObject
{
    Q_OBJECT

public:
    static void keyPressEvent(QKeyEvent *event);
    static void keyReleaseEvent(QKeyEvent *event);

    virtual bool eventFilter(QObject *obj, QEvent *event);

signals:
    void keyStateChanged(const QString &keyName, bool pressed);
};

extern QtKeypadBridge qt_keypad_bridge;

/* Map a keymap ID (row*KEYPAD_COLS + col) to a human-readable name. */
const char *keyIdToName(unsigned int id);

/* Set a key state by keymap ID.  Notifies both the emulation core and the
 * QML bridge (button highlight + key history signal). */
void setKeypad(unsigned int keymap_id, bool state);

#endif // QTKEYPADBRIDGE_H
