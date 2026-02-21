#ifndef APP_BASELINELAYOUT_H
#define APP_BASELINELAYOUT_H

#include <QDockWidget>

#include <array>
#include <limits>

namespace BaselineLayout {

inline constexpr const char *kLayoutSchema = "firebird.kdd.layout.v1";
inline constexpr int kSerializationVersion = 3;
inline constexpr const char *kDebugDockStateSchema = "firebird.debug.dockstate.v1";
inline constexpr const char *kCoreDockConnectionsSchema = "firebird.core.connections.v1";

struct RectRule {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SizeRule {
    int width = 0;
    int height = 0;
};

struct ScreenInfoRule {
    int index = 0;
    const char *name = nullptr;
    double devicePixelRatio = 1.0;
    RectRule geometry = {};
};

// Typed copy of decoded layoutBase64.screenInfo.
inline constexpr std::array<ScreenInfoRule, 1> kScreenInfoRules = {{
    { 0, "Built-in Display", 1.0, { 0, 0, 2624, 1640 } },
}};

struct MainWindowRule {
    const char *uniqueName = nullptr;
    bool isVisible = false;
    int options = 0;
    int screenIndex = 0;
    SizeRule screenSize = {};
    int windowState = 0;
    RectRule geometry = {};
    RectRule normalGeometry = {};
    int affinityCount = 0;
};

// Typed copy of decoded layoutBase64.mainWindows metadata.
inline constexpr std::array<MainWindowRule, 1> kMainWindowRules = {{
    { "contentWindow", true, 37, 0, { 2624, 1640 }, 2, { 1, 63, 2622, 1525 }, { 0, 0, 0, 0 } },
}};

struct DockPlaceholderRule {
    bool isFloatingWindow = false;
    int itemIndex = -1;
    const char *mainWindowUniqueName = nullptr;
};

struct DockLastPositionRule {
    RectRule lastFloatingGeometry = {};
    int tabIndex = -1;
    bool wasFloating = false;
    DockPlaceholderRule placeholder = {};
    int lastOverlayedGeometryCount = 0;
    int placeholderCount = 1;
};

struct AllDockWidgetRule {
    const char *uniqueName = nullptr;
    int lastCloseReason = 0;
    DockLastPositionRule lastPosition = {};
};

// Typed copy of decoded layoutBase64.allDockWidgets.
inline constexpr std::array<AllDockWidgetRule, 22> kAllDockWidgetRules = {{
    { "-persistentCentralDockWidget", 0, { { 0, 0, 0, 0 }, -1, false, { false, 5, "contentWindow" } } },
    { "dockLCD", 0, { { 0, 0, 0, 0 }, -1, false, { false, 2, "contentWindow" } } },
    { "dockControls", 0, { { 0, 0, 0, 0 }, -1, false, { false, 3, "contentWindow" } } },
    { "tabFiles", 0, { { 0, 0, 0, 0 }, 0, false, { false, 0, "contentWindow" } } },
    { "tab", 0, { { 0, 0, 0, 0 }, 0, false, { false, 4, "contentWindow" } } },
    { "dockNandBrowser", 0, { { 0, 0, 0, 0 }, 0, false, { false, 0, "contentWindow" } } },
    { "dockHwConfig", 0, { { 0, 0, 0, 0 }, 2, false, { false, 1, "contentWindow" } } },
    { "dockExternalLCD", 0, { { 0, 0, 0, 0 }, 0, false, { false, 8, "contentWindow" } } },
    { "dockDisasm", 0, { { 0, 0, 0, 0 }, 0, false, { false, 7, "contentWindow" } } },
    { "dockRegisters", 0, { { 0, 0, 0, 0 }, 0, false, { false, 9, "contentWindow" } } },
    { "dockStack", 0, { { 0, 0, 0, 0 }, 0, false, { false, 9, "contentWindow" } } },
    { "dockMemory", 0, { { 0, 0, 0, 0 }, 0, false, { false, 6, "contentWindow" } } },
    { "dockBreakpoints", 0, { { 0, 0, 0, 0 }, 0, false, { false, 10, "contentWindow" } } },
    { "dockWatchpoints", 0, { { 0, 0, 0, 0 }, 0, false, { false, 10, "contentWindow" } } },
    { "dockPortMonitor", 0, { { 0, 0, 0, 0 }, 0, false, { false, 14, "contentWindow" } } },
    { "dockKeyHistory", 0, { { 0, 0, 0, 0 }, 0, false, { false, 13, "contentWindow" } } },
    { "dockConsole", 0, { { 43, 1273, 2163, 262 }, 4, false, { false, 11, "contentWindow" } } },
    { "dockMemVis", 0, { { 0, 0, 0, 0 }, 0, false, { false, 6, "contentWindow" } } },
    { "dockCycleCounter", 1, { { 974, 1334, 1655, 293 }, 0, true, { false, 14, "contentWindow" } } },
    { "dockTimerMonitor", 0, { { 0, 0, 0, 0 }, 0, false, { false, 14, "contentWindow" } } },
    { "dockLCDState", 0, { { 0, 0, 0, 0 }, 4, false, { false, 12, "contentWindow" } } },
    { "dockMMUViewer", 0, { { 0, 0, 0, 0 }, 0, false, { false, 6, "contentWindow" } } },
}};

// Typed copy of decoded layoutBase64.closedDockWidgets.
inline constexpr std::array<const char *, 8> kClosedDockWidgetNames = {{
    "dockNandBrowser",
    "dockExternalLCD",
    "dockStack",
    "dockBreakpoints",
    "dockMemVis",
    "dockCycleCounter",
    "dockTimerMonitor",
    "dockMMUViewer",
}};

struct FloatingWindowRule {
    const char *uniqueName = nullptr;
    RectRule geometry = {};
    bool isVisible = false;
    int options = 0;
};

// Typed copy of decoded layoutBase64.floatingWindows (empty in baseline).
inline constexpr std::array<FloatingWindowRule, 0> kFloatingWindowRules = {{}};

struct DecodedFrameRule {
    const char *frameId = nullptr;
    const char *objectName = nullptr;
    std::array<const char *, 4> dockWidgets = {{ nullptr, nullptr, nullptr, nullptr }};
    int dockCount = 0;
    int currentTabIndex = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isNull = false;
    int options = 0;
    const char *mainWindowUniqueName = nullptr;
};

// Full typed frame map from decoded layoutBase64.mainWindows[0].multiSplitterLayout.frames.
inline constexpr std::array<DecodedFrameRule, 14> kDecodedFrameRules = {{
    { "5", "-persistentCentralDockWidget", { "-persistentCentralDockWidget", nullptr, nullptr, nullptr }, 1, 0, 945, 0, 1244, 222, false, 10, "contentWindow" },
    { "21722", "tabFiles", { "tabFiles", "dockNandBrowser", nullptr, nullptr }, 2, 0, 0, 0, 448, 1035, false, 0, "contentWindow" },
    { "130173", "dockHwConfig", { "dockHwConfig", nullptr, nullptr, nullptr }, 1, 0, 0, 1040, 448, 449, false, 0, "contentWindow" },
    { "21690", "dockLCD", { "dockLCD", nullptr, nullptr, nullptr }, 1, 0, 453, 0, 487, 387, false, 0, "contentWindow" },
    { "21563", "dockControls", { "dockControls", nullptr, nullptr, nullptr }, 1, 0, 453, 392, 487, 30, false, 0, "contentWindow" },
    { "84443", "tab", { "tab", nullptr, nullptr, nullptr }, 1, 0, 453, 487, 487, 677, false, 0, "contentWindow" },
    { "21465", "dockMemory", { "dockMemory", "dockMemVis", "dockMMUViewer", nullptr }, 3, 0, 945, 227, 1244, 403, false, 0, "contentWindow" },
    { "21658", "dockDisasm", { "dockDisasm", nullptr, nullptr, nullptr }, 1, 0, 945, 635, 1244, 529, false, 0, "contentWindow" },
    { "21595", "dockRegisters", { "dockRegisters", "dockStack", nullptr, nullptr }, 2, 0, 2194, 0, 426, 786, false, 0, "contentWindow" },
    { "272701", "dockWatchpoints", { "dockBreakpoints", "dockWatchpoints", nullptr, nullptr }, 2, 1, 2194, 791, 426, 373, false, 0, "contentWindow" },
    { "254633", "dockConsole", { "dockConsole", nullptr, nullptr, nullptr }, 1, 0, 453, 1169, 738, 320, false, 0, "contentWindow" },
    { "221377", "dockLCDState", { "dockLCDState", nullptr, nullptr, nullptr }, 1, 0, 1196, 1169, 471, 320, false, 0, "contentWindow" },
    { "230061", "dockKeyHistory", { "dockKeyHistory", nullptr, nullptr, nullptr }, 1, 0, 1672, 1169, 518, 320, false, 0, "contentWindow" },
    { "21187", "dockPortMonitor", { "dockPortMonitor", "dockTimerMonitor", nullptr, nullptr }, 2, 0, 2195, 1169, 425, 320, false, 0, "contentWindow" },
}};

struct DecodedPlacementRule {
    const char *frameId = nullptr;
    const char *relativeFrameId = nullptr;
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
};

// Decoded placement chain from the old KDD baseline payload tree.
inline constexpr std::array<DecodedPlacementRule, 13> kDecodedPlacementRules = {{
    { "21722", nullptr, Qt::LeftDockWidgetArea },
    { "130173", "21722", Qt::BottomDockWidgetArea },
    { "21690", "21722", Qt::RightDockWidgetArea },
    { "21563", "21690", Qt::BottomDockWidgetArea },
    { "84443", "21563", Qt::BottomDockWidgetArea },
    { "21465", "5", Qt::BottomDockWidgetArea },
    { "21658", "21465", Qt::BottomDockWidgetArea },
    { "21595", "5", Qt::RightDockWidgetArea },
    { "272701", "21595", Qt::BottomDockWidgetArea },
    { "254633", "21690", Qt::BottomDockWidgetArea },
    { "221377", "254633", Qt::RightDockWidgetArea },
    { "230061", "221377", Qt::RightDockWidgetArea },
    { "21187", "230061", Qt::RightDockWidgetArea },
}};

struct DecodedLayoutNodeRule {
    bool isContainer = false;
    int orientation = 0;
    const char *frameId = nullptr;
    std::array<int, 4> children = {{ -1, -1, -1, -1 }};
    int childCount = 0;
    bool isVisible = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int minWidth = 0;
    int minHeight = 0;
    int maxWidth = 0;
    int maxHeight = 0;
    double percentageWithinParent = 0.0;
};

// Full decoded splitter/container tree from the old KDD baseline payload.
inline constexpr std::array<DecodedLayoutNodeRule, 24> kDecodedLayoutTree = {{
    { true, 1, nullptr, { 1, 4, -1, -1 }, 2, false, 0, 0, 2620, 1489, 80, 90, 16777215, 16777215, 0.0 },
    { true, 2, nullptr, { 2, 3, -1, -1 }, 2, false, 0, 0, 448, 1489, 80, 90, 16777215, 16777215, 0.17131931166347991 },
    { false, 0, "21722", { -1, -1, -1, -1 }, 0, true, 0, 0, 448, 1035, 341, 168, 524291, 524347, 0.69743935309973049 },
    { false, 0, "130173", { -1, -1, -1, -1 }, 0, true, 0, 1040, 448, 449, 209, 424, 524291, 524321, 0.30256064690026951 },
    { true, 2, nullptr, { 5, 19, -1, -1 }, 2, false, 453, 0, 2167, 1489, 1129, 768, 1048755, 1048673, 0.82868068833652009 },
    { true, 1, nullptr, { 6, 10, 15, 16 }, 4, false, 0, 0, 2167, 1164, 1190, 963, 1572879, 1048647, 0.78436657681940702 },
    { true, 2, nullptr, { 7, 8, 9, -1 }, 3, false, 0, 0, 487, 1164, 324, 403, 524291, 1048647, 0.22577654149281409 },
    { false, 0, "21690", { -1, -1, -1, -1 }, 0, true, 0, 0, 487, 387, 324, 274, 524291, 524321, 0.33535528596187175 },
    { false, 0, "21563", { -1, -1, -1, -1 }, 0, true, 0, 392, 487, 90, 426, 90, 524291, 90, 0.077989601386481797 },
    { false, 0, "84443", { -1, -1, -1, -1 }, 0, true, 0, 487, 487, 677, 269, 124, 524291, 524321, 0.58665511265164649 },
    { true, 2, nullptr, { 11, 13, 14, -1 }, 3, false, 492, 0, 1244, 1164, 430, 568, 524287, 1572935, 0.57672693555864629 },
    { true, 1, nullptr, { 12, -1, -1, -1 }, 1, false, 0, 0, 1244, 222, 537, 164, 1572883, 524291, 0.19237435008665510 },
    { false, 0, "5", { -1, -1, -1, -1 }, 0, true, 0, 0, 1244, 222, 224, 164, 524291, 524291, 1.0 },
    { false, 0, "21465", { -1, -1, -1, -1 }, 0, true, 0, 227, 1244, 403, 430, 304, 524291, 524347, 0.34922010398613518 },
    { false, 0, "21658", { -1, -1, -1, -1 }, 0, true, 0, 635, 1244, 529, 80, 90, 524287, 524287, 0.45840554592720972 },
    { false, 0, nullptr, { -1, -1, -1, -1 }, 0, false, 0, 0, 211, 480, 80, 90, 16777215, 16777215, 0.0 },
    { true, 2, nullptr, { 17, 18, -1, -1 }, 2, false, 1741, 0, 426, 1164, 426, 963, 524291, 1048768, 0.19749652294853964 },
    { false, 0, "21595", { -1, -1, -1, -1 }, 0, true, 0, 0, 426, 786, 263, 439, 524291, 524347, 0.67817083692838653 },
    { false, 0, "272701", { -1, -1, -1, -1 }, 0, true, 0, 791, 426, 373, 106, 145, 524313, 524342, 0.32182916307161347 },
    { true, 1, nullptr, { 20, 21, 22, 23 }, 4, false, 0, 1169, 2167, 320, 391, 195, 1048755, 524321, 0.21563342318059300 },
    { false, 0, "254633", { -1, -1, -1, -1 }, 0, true, 0, 0, 738, 320, 80, 144, 524287, 524321, 0.34293680297397772 },
    { false, 0, "221377", { -1, -1, -1, -1 }, 0, true, 743, 0, 471, 320, 134, 116, 524291, 524313, 0.21886617100371747 },
    { false, 0, "230061", { -1, -1, -1, -1 }, 0, true, 1219, 0, 518, 320, 169, 150, 524291, 524321, 0.24070631970260223 },
    { false, 0, "21187", { -1, -1, -1, -1 }, 0, true, 1742, 0, 425, 320, 134, 150, 524291, 524347, 0.19749070631970261 },
}};

inline constexpr int kDecodedLayoutRootNodeIndex = 0;

struct DockProfileEntry {
    const char *objectName = nullptr;
    const char *title = nullptr;
    bool visible = false;
    bool floating = false;
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    RectRule geometry = {};
};

// Typed copy of default.json docks + hidden dock defaults not present in that list.
inline constexpr std::array<DockProfileEntry, 21> kDockProfileEntries = {{
    { "dockLCD",          "Screen (147%)",     true,  false, Qt::NoDockWidgetArea, { 0, 0, 482, 352 } },
    { "dockStack",        "Stack",             false, false, Qt::NoDockWidgetArea, { 0, 0, 562, 789 } },
    { "dockRegisters",    "Registers",         true,  false, Qt::NoDockWidgetArea, { 0, 0, 421, 727 } },
    { "dockTimerMonitor", "Timer Monitor",     false, false, Qt::NoDockWidgetArea, { 0, 0, 1076, 261 } },
    { "dockPortMonitor",  "Port Monitor",      true,  false, Qt::NoDockWidgetArea, { 0, 0, 420, 261 } },
    { "tab",              "Keypad",            true,  false, Qt::NoDockWidgetArea, { 0, 0, 482, 642 } },
    { "dockDisasm",       "Disassembly",       true,  false, Qt::NoDockWidgetArea, { 0, 0, 1239, 494 } },
    { "dockMemVis",       "Memory Visualizer", false, false, Qt::NoDockWidgetArea, { 0, 0, 2615, 191 } },
    { "dockMMUViewer",    "MMU Viewer",        false, false, Qt::NoDockWidgetArea, { 0, 0, 2615, 625 } },
    { "dockMemory",       "Memory",            true,  false, Qt::NoDockWidgetArea, { 0, 0, 1239, 344 } },
    { "dockNandBrowser",  "NAND Browser",      false, false, Qt::NoDockWidgetArea, { 0, 0, 443, 1430 } },
    { "tabFiles",         "File Transfer",     true,  false, Qt::NoDockWidgetArea, { 0, 0, 443, 976 } },
    { "dockConsole",      "Console",           true,  false, Qt::NoDockWidgetArea, { 0, 0, 733, 285 } },
    { "dockHwConfig",     "Hardware Config",   true,  false, Qt::NoDockWidgetArea, { 0, 0, 443, 414 } },
    { "dockBreakpoints",  "Breakpoints",       false, false, Qt::NoDockWidgetArea, { 0, 0, 421, 314 } },
    { "dockWatchpoints",  "Watchpoints",       true,  false, Qt::NoDockWidgetArea, { 0, 0, 421, 314 } },
    { "dockKeyHistory",   "Key History",       true,  false, Qt::NoDockWidgetArea, { 0, 0, 513, 285 } },
    { "dockLCDState",     "LCD State",         true,  false, Qt::NoDockWidgetArea, { 0, 0, 466, 285 } },
    { "dockControls",     "Controls",          true,  false, Qt::NoDockWidgetArea, { 0, 0, 482, 26 } },
    { "dockCycleCounter", "Cycle Counter",     false, false, Qt::NoDockWidgetArea, { 974, 1334, 1655, 293 } },
    { "dockExternalLCD",  "External LCD",      false, false, Qt::NoDockWidgetArea, { 0, 0, 0, 0 } },
}};

struct CoreDockConnectionRule {
    const char *a = nullptr;
    const char *b = nullptr;
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
};

// Baseline default currently has no pre-connected core dock pairs.
inline constexpr std::array<CoreDockConnectionRule, 0> kCoreDockConnectionRules = {{}};

inline constexpr int kUnsetInt = std::numeric_limits<int>::min();

struct DebugDockStateRule {
    const char *dockId = nullptr;
    const char *baseAddr = nullptr;
    const char *searchText = nullptr;
    int displayFormat = kUnsetInt;
    int modeIndex = kUnsetInt;
    int searchType = kUnsetInt;
    int selectedOffset = kUnsetInt;
    int showAscii = -1;
    const char *filterText = nullptr;
    int fontSize = kUnsetInt;
    bool includeEmptyCommandHistory = false;
    int maxBlockCount = kUnsetInt;
    int autoRefresh = -1;
    int bpp = kUnsetInt;
    int imageHeight = kUnsetInt;
    int imageWidth = kUnsetInt;
    int zoom = kUnsetInt;
    int refreshIndex = kUnsetInt;
};

inline constexpr std::array<DebugDockStateRule, 8> kDebugDockStateRules = {{
    { "dockDisasm", "a40011bc", "" },
    { "dockRegisters", nullptr, nullptr, 0, 0 },
    { "dockMemory", "00000000", "", kUnsetInt, kUnsetInt, 0, 0, 1 },
    { "dockKeyHistory", nullptr, nullptr, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, -1, "", 9 },
    { "dockConsole", nullptr, nullptr, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, -1, "",
      kUnsetInt, true, 5000 },
    { "dockMemVis", "c0000000", nullptr, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, -1, nullptr,
      kUnsetInt, false, kUnsetInt, 0, 3, 240, 320, 2 },
    { "dockTimerMonitor", nullptr, nullptr, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, -1,
      nullptr, kUnsetInt, false, kUnsetInt, -1, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, 0 },
    { "dockLCDState", nullptr, nullptr, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, -1,
      nullptr, kUnsetInt, false, kUnsetInt, -1, kUnsetInt, kUnsetInt, kUnsetInt, kUnsetInt, 0 },
}};

} // namespace BaselineLayout

#endif // APP_BASELINELAYOUT_H
