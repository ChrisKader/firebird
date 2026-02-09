#ifndef MATERIALICONS_H
#define MATERIALICONS_H

#include <QFont>
#include <QIcon>
#include <QColor>

namespace MaterialIcons {

/* Material Symbols Rounded codepoints */
namespace CP {
    constexpr ushort Folder       = 0xE2C7;
    constexpr ushort BugReport    = 0xE868;
    constexpr ushort Terminal     = 0xEB8B;
    constexpr ushort Keyboard     = 0xE312;
    constexpr ushort Storage      = 0xE1DB;
    constexpr ushort Code         = 0xE86F;
    constexpr ushort Memory       = 0xE322;
    constexpr ushort Layers       = 0xE53B;
    constexpr ushort Timer        = 0xE425;
    constexpr ushort Visibility   = 0xE8F4;
    constexpr ushort History      = 0xE889;
    constexpr ushort GridOn       = 0xE06F;
    constexpr ushort Display      = 0xE30B;
    constexpr ushort AvTimer      = 0xE01B;
    constexpr ushort StopCircle   = 0xEF4B;
    constexpr ushort Image        = 0xE3F4;
    constexpr ushort ZoomIn       = 0xE8FF;
    constexpr ushort ZoomOut      = 0xE900;
    constexpr ushort Link         = 0xE157;
    constexpr ushort LinkOff      = 0xE16F;
    constexpr ushort Settings     = 0xE8B8;
    constexpr ushort Tune         = 0xE429;
    constexpr ushort Refresh      = 0xE5D5;
    constexpr ushort Delete       = 0xE14C;
    constexpr ushort Play         = 0xE037;
    constexpr ushort Pause        = 0xE034;
    constexpr ushort Screenshot   = 0xE412;
    constexpr ushort USB          = 0xE1E0;
    constexpr ushort Speed        = 0xE9E4;
    constexpr ushort DarkMode     = 0xE51C;
    constexpr ushort LightMode    = 0xE518;
    constexpr ushort Bookmark     = 0xE866;
    constexpr ushort List         = 0xE896;
    constexpr ushort TableChart   = 0xE265;
    constexpr ushort Monitor      = 0xEF44;
    constexpr ushort DataObject   = 0xE1BF;
    constexpr ushort CycleCounter = 0xE514;  /* bar_chart */
    constexpr ushort ViewColumn   = 0xE8A2;  /* view_column for stack */
    constexpr ushort FolderOpen   = 0xE2C8;
    constexpr ushort Save         = 0xE161;
    constexpr ushort Build        = 0xF8AE;  /* handyman */
    constexpr ushort Add          = 0xE145;
}

/* Render a Material icon glyph to a QIcon. */
QIcon fromCodepoint(const QFont &font, ushort codepoint, int size, const QColor &color);

/* Convenience: render with default size (16px). */
inline QIcon fromCodepoint(const QFont &font, ushort codepoint, const QColor &color)
{
    return fromCodepoint(font, codepoint, 16, color);
}

} // namespace MaterialIcons

#endif // MATERIALICONS_H
