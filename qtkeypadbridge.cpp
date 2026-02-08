#include "qtkeypadbridge.h"

#include <cassert>
#include "keymap.h"
#include "core/keypad.h"
#include "qmlbridge.h"
#include <QHash>

QtKeypadBridge qt_keypad_bridge;

static const char *keyIdToName(unsigned int id)
{
    switch (id) {
    case keymap::ret:    return "ret";
    case keymap::enter:  return "enter";
    case keymap::neg:    return "(-)"  ;
    case keymap::space:  return "space";
    case keymap::on:     return "on";
    case keymap::esc:    return "esc";
    case keymap::pad:    return "pad";
    case keymap::tab:    return "tab";
    case keymap::doc:    return "doc";
    case keymap::menu:   return "menu";
    case keymap::ctrl:   return "ctrl";
    case keymap::shift:  return "shift";
    case keymap::var:    return "var";
    case keymap::del:    return "del";
    case keymap::ee:     return "EE";
    case keymap::pi:     return "pi";
    case keymap::comma:  return ",";
    case keymap::punct:  return "?!";
    case keymap::flag:   return "flag";
    case keymap::n0:     return "0";
    case keymap::n1:     return "1";
    case keymap::n2:     return "2";
    case keymap::n3:     return "3";
    case keymap::n4:     return "4";
    case keymap::n5:     return "5";
    case keymap::n6:     return "6";
    case keymap::n7:     return "7";
    case keymap::n8:     return "8";
    case keymap::n9:     return "9";
    case keymap::dot:    return ".";
    case keymap::equ:    return "=";
    case keymap::trig:   return "trig";
    case keymap::pow:    return "^";
    case keymap::squ:    return "x^2";
    case keymap::exp:    return "e^x";
    case keymap::pow10:  return "10^x";
    case keymap::pleft:  return "(";
    case keymap::pright: return ")";
    case keymap::metrix: return "sto";
    case keymap::cat:    return "cat";
    case keymap::mult:   return "*";
    case keymap::div:    return "/";
    case keymap::plus:   return "+";
    case keymap::minus:  return "-";
    case keymap::aa:     return "a";
    case keymap::ab:     return "b";
    case keymap::ac:     return "c";
    case keymap::ad:     return "d";
    case keymap::ae:     return "e";
    case keymap::af:     return "f";
    case keymap::ag:     return "g";
    case keymap::ah:     return "h";
    case keymap::ai:     return "i";
    case keymap::aj:     return "j";
    case keymap::ak:     return "k";
    case keymap::al:     return "l";
    case keymap::am:     return "m";
    case keymap::an:     return "n";
    case keymap::ao:     return "o";
    case keymap::ap:     return "p";
    case keymap::aq:     return "q";
    case keymap::ar:     return "r";
    case keymap::as:     return "s";
    case keymap::at:     return "t";
    case keymap::au:     return "u";
    case keymap::av:     return "v";
    case keymap::aw:     return "w";
    case keymap::ax:     return "x";
    case keymap::ay:     return "y";
    case keymap::az:     return "z";
    default:             return "??";
    }
}

void setKeypad(unsigned int keymap_id, bool state)
{
    int col = keymap_id % KEYPAD_COLS, row = keymap_id / KEYPAD_COLS;
    assert(row < KEYPAD_ROWS);
    //assert(col < KEYPAD_COLS); Not needed.

    ::keypad_set_key(row, col, state);
    the_qml_bridge->notifyButtonStateChanged(row, col, state);

    emit qt_keypad_bridge.keyStateChanged(
        QString::fromLatin1(keyIdToName(keymap_id)), state);
}

static QHash<int, int> pressed_keys;

void keyToKeypad(QKeyEvent *event)
{
    static const int ALT   = 0x02000000;
    static const QHash<int, int> QtKeyMap {
            // Touchpad left buttons
        {Qt::Key_Escape, keymap::esc}
        ,{Qt::Key_End, keymap::pad}
        ,{Qt::Key_Tab, keymap::tab}

            // Touchpad right buttons
        ,{Qt::Key_Home, keymap::on}
        ,{Qt::Key_Escape | ALT, keymap::on}
        ,{Qt::Key_PageUp, keymap::doc}
        ,{Qt::Key_D | ALT, keymap::doc}
        ,{Qt::Key_PageDown, keymap::menu}
        ,{Qt::Key_M | ALT, keymap::menu}

            // Touchpad bottom buttons
        ,{Qt::Key_Control, keymap::ctrl}
        ,{Qt::Key_Shift, keymap::shift}
        ,{Qt::Key_Insert, keymap::var}
        ,{Qt::Key_V | ALT, keymap::var}
        ,{Qt::Key_Backspace, keymap::del}
        ,{Qt::Key_Delete, keymap::del}

            // Alpha buttons
        ,{Qt::Key_A, keymap::aa}
        ,{Qt::Key_B, keymap::ab}
        ,{Qt::Key_C, keymap::ac}
        ,{Qt::Key_D, keymap::ad}
        ,{Qt::Key_E, keymap::ae}
        ,{Qt::Key_F, keymap::af}
        ,{Qt::Key_G, keymap::ag}
        ,{Qt::Key_H, keymap::ah}
        ,{Qt::Key_I, keymap::ai}
        ,{Qt::Key_J, keymap::aj}
        ,{Qt::Key_K, keymap::ak}
        ,{Qt::Key_L, keymap::al}
        ,{Qt::Key_M, keymap::am}
        ,{Qt::Key_N, keymap::an}
        ,{Qt::Key_O, keymap::ao}
        ,{Qt::Key_P, keymap::ap}
        ,{Qt::Key_Q, keymap::aq}
        ,{Qt::Key_R, keymap::ar}
        ,{Qt::Key_S, keymap::as}
        ,{Qt::Key_T, keymap::at}
        ,{Qt::Key_U, keymap::au}
        ,{Qt::Key_V, keymap::av}
        ,{Qt::Key_W, keymap::aw}
        ,{Qt::Key_X, keymap::ax}
        ,{Qt::Key_Y, keymap::ay}
        ,{Qt::Key_Z, keymap::az}
        ,{Qt::Key_Less, keymap::ee}
        ,{Qt::Key_Less | ALT, keymap::ee}
        ,{Qt::Key_E | ALT, keymap::ee}
        ,{Qt::Key_Bar, keymap::pi}
        ,{Qt::Key_Bar | ALT, keymap::pi}
        ,{Qt::Key_Comma, keymap::comma}
        ,{Qt::Key_Comma | ALT, keymap::comma}
        ,{Qt::Key_Question, keymap::punct}
        ,{Qt::Key_Question | ALT, keymap::punct}
        ,{Qt::Key_W | ALT, keymap::punct}
        ,{Qt::Key_Greater, keymap::flag}
        ,{Qt::Key_Greater | ALT, keymap::flag}
        ,{Qt::Key_F | ALT, keymap::flag}
        ,{Qt::Key_Space, keymap::space}
        ,{Qt::Key_Enter | ALT, keymap::ret}
        ,{Qt::Key_Return | ALT, keymap::ret}

            // Numpad buttons
        ,{Qt::Key_0, keymap::n0}
        ,{Qt::Key_1, keymap::n1}
        ,{Qt::Key_2, keymap::n2}
        ,{Qt::Key_3, keymap::n3}
        ,{Qt::Key_4, keymap::n4}
        ,{Qt::Key_5, keymap::n5}
        ,{Qt::Key_6, keymap::n6}
        ,{Qt::Key_7, keymap::n7}
        ,{Qt::Key_8, keymap::n8}
        ,{Qt::Key_9, keymap::n9}
        ,{Qt::Key_Period, keymap::dot}
        ,{Qt::Key_Period | ALT, keymap::dot}
        ,{Qt::Key_Minus | ALT, keymap::neg}
        ,{Qt::Key_QuoteLeft, keymap::neg}
        ,{Qt::Key_QuoteLeft | ALT, keymap::neg}

            // Left buttons
        ,{Qt::Key_Equal, keymap::equ}
        ,{Qt::Key_Equal | ALT, keymap::equ}
        ,{Qt::Key_Q | ALT, keymap::equ}
        ,{Qt::Key_Backslash, keymap::trig}
        ,{Qt::Key_Backslash | ALT, keymap::trig}
        ,{Qt::Key_T | ALT, keymap::trig}
        ,{Qt::Key_AsciiCircum, keymap::pow}
        ,{Qt::Key_AsciiCircum | ALT, keymap::pow}
        ,{Qt::Key_P | ALT, keymap::pow}
        ,{Qt::Key_At, keymap::squ}
        ,{Qt::Key_At | ALT, keymap::squ}
        ,{Qt::Key_2 | ALT, keymap::squ}
        ,{Qt::Key_BracketLeft, keymap::exp}
        ,{Qt::Key_BracketLeft | ALT, keymap::exp}
        ,{Qt::Key_X | ALT, keymap::exp}
        ,{Qt::Key_BracketRight, keymap::pow10}
        ,{Qt::Key_BracketRight | ALT, keymap::pow10}
        ,{Qt::Key_1 | ALT, keymap::pow10}
        ,{Qt::Key_ParenLeft, keymap::pleft}
        ,{Qt::Key_ParenLeft | ALT, keymap::pleft}
        ,{Qt::Key_F1, keymap::pleft}
        ,{Qt::Key_ParenRight, keymap::pright}
        ,{Qt::Key_ParenRight | ALT, keymap::pright}
        ,{Qt::Key_F2, keymap::pright}

            // Right buttons
        ,{Qt::Key_Semicolon, keymap::metrix}
        ,{Qt::Key_Semicolon | ALT, keymap::metrix}
        ,{Qt::Key_O | ALT, keymap::metrix}
        ,{Qt::Key_Apostrophe, keymap::cat}
        ,{Qt::Key_Apostrophe | ALT, keymap::cat}
        ,{Qt::Key_C | ALT, keymap::cat}
        ,{Qt::Key_Asterisk, keymap::mult}
        ,{Qt::Key_Asterisk | ALT, keymap::mult}
        ,{Qt::Key_A | ALT, keymap::mult}
        ,{Qt::Key_Slash, keymap::div}
        ,{Qt::Key_Slash | ALT, keymap::div}
        ,{Qt::Key_F3, keymap::div}
        ,{Qt::Key_Plus, keymap::plus}
        ,{Qt::Key_Plus | ALT, keymap::plus}
        ,{Qt::Key_Equal | ALT, keymap::plus}
        ,{Qt::Key_Minus, keymap::minus}
        ,{Qt::Key_Minus | ALT, keymap::minus}
        ,{Qt::Key_Underscore, keymap::minus}
        ,{Qt::Key_Underscore | ALT, keymap::minus}
        ,{Qt::Key_Enter, keymap::enter}
        ,{Qt::Key_Return, keymap::enter}
    };

    // Determine physical key that correspond to the key we got,
    // to be able to get releases reliably if modifiers change in between:
    // Press shift, press 2 (-> "), release shift, release 2 (-> 2)
    // results in press of 2 but release of " (example for de layout).
    auto physkey = event->nativeScanCode();
    if (physkey < 1)
        physkey = event->key(); // (Bad) fallback to the virtual key if needed

    auto pressed = pressed_keys.find(physkey);

    // If physkey is already pressed, then this must the the release event
    if (pressed != pressed_keys.end())
    {
        setKeypad(*pressed, false);
        pressed_keys.erase(pressed);
    }
    else if (event->type() == QEvent::KeyPress) // But press only on the press event
    {
        auto mkey = event->key();

        if (event->modifiers() & Qt::ShiftModifier && mkey == Qt::Key_Alt)
        {
            setKeypad(keymap::shift, false);
            return;
        }

        if (event->modifiers() & Qt::AltModifier)
        {
            if (mkey == Qt::Key_Shift)
                return; // Just ignore it
            else
                mkey |= ALT; // Compose alt into the unused bit of the keycode
        }

        auto translated = QtKeyMap.find(mkey);

        if (translated != QtKeyMap.end())
        {
            pressed_keys.insert(physkey, *translated);
            setKeypad(*translated, true);
        }
    }
}

void QtKeypadBridge::keyPressEvent(QKeyEvent *event)
{
    // Ignore autorepeat, calc os must handle it on its own
    if(event->isAutoRepeat())
        return;

    Qt::Key key = static_cast<Qt::Key>(event->key());

    switch(key)
    {
    case Qt::Key_Down:
        keypad.touchpad_x = TOUCHPAD_X_MAX / 2;
        keypad.touchpad_y = 0;
        emit qt_keypad_bridge.keyStateChanged(QStringLiteral("down"), true);
        break;
    case Qt::Key_Up:
        keypad.touchpad_x = TOUCHPAD_X_MAX / 2;
        keypad.touchpad_y = TOUCHPAD_Y_MAX;
        emit qt_keypad_bridge.keyStateChanged(QStringLiteral("up"), true);
        break;
    case Qt::Key_Left:
        keypad.touchpad_y = TOUCHPAD_Y_MAX / 2;
        keypad.touchpad_x = 0;
        emit qt_keypad_bridge.keyStateChanged(QStringLiteral("left"), true);
        break;
    case Qt::Key_Right:
        keypad.touchpad_y = TOUCHPAD_Y_MAX / 2;
        keypad.touchpad_x = TOUCHPAD_X_MAX;
        emit qt_keypad_bridge.keyStateChanged(QStringLiteral("right"), true);
        break;
    default:
        keyToKeypad(event);

        return;
    }

    keypad.touchpad_contact = keypad.touchpad_down = true;
    the_qml_bridge->touchpadStateChanged();
    keypad.kpc.gpio_int_active |= 0x800;

    keypad_int_check();
}

void QtKeypadBridge::keyReleaseEvent(QKeyEvent *event)
{
    // Ignore autorepeat, calc os must handle it on its own
    if(event->isAutoRepeat())
        return;

    Qt::Key key = static_cast<Qt::Key>(event->key());

    switch(key)
    {
    case Qt::Key_Down:
        if(keypad.touchpad_x == TOUCHPAD_X_MAX / 2
            && keypad.touchpad_y == 0)
        {
            keypad.touchpad_contact = keypad.touchpad_down = false;
            emit qt_keypad_bridge.keyStateChanged(QStringLiteral("down"), false);
        }
        break;
    case Qt::Key_Up:
        if(keypad.touchpad_x == TOUCHPAD_X_MAX / 2
            && keypad.touchpad_y == TOUCHPAD_Y_MAX)
        {
            keypad.touchpad_contact = keypad.touchpad_down = false;
            emit qt_keypad_bridge.keyStateChanged(QStringLiteral("up"), false);
        }
        break;
    case Qt::Key_Left:
        if(keypad.touchpad_y == TOUCHPAD_Y_MAX / 2
            && keypad.touchpad_x == 0)
        {
            keypad.touchpad_contact = keypad.touchpad_down = false;
            emit qt_keypad_bridge.keyStateChanged(QStringLiteral("left"), false);
        }
        break;
    case Qt::Key_Right:
        if(keypad.touchpad_y == TOUCHPAD_Y_MAX / 2
            && keypad.touchpad_x == TOUCHPAD_X_MAX)
        {
            keypad.touchpad_contact = keypad.touchpad_down = false;
            emit qt_keypad_bridge.keyStateChanged(QStringLiteral("right"), false);
        }
        break;
    default:
        keyToKeypad(event);

        return;
    }

    the_qml_bridge->touchpadStateChanged();
    keypad.kpc.gpio_int_active |= 0x800;
    keypad_int_check();
}

bool QtKeypadBridge::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);

    if(event->type() == QEvent::KeyPress)
        keyPressEvent(static_cast<QKeyEvent*>(event));
    else if(event->type() == QEvent::KeyRelease)
        keyReleaseEvent(static_cast<QKeyEvent*>(event));
    else if(event->type() == QEvent::FocusOut)
    {
        // Release all keys on focus change
        for(auto calc_key : pressed_keys)
            setKeypad(calc_key, false);

        pressed_keys.clear();
        return false;
    }
    else
        return false;

    return true;
}
