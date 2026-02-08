#!/usr/bin/env python3
"""
regen_svg_paths.py — Regenerate qml/SvgPaths.qml from g_elements.jsonl

This script reads SVG element data extracted from the TI-Nspire CX II
calculator artwork (symbols.html → g_elements.jsonl) and produces a QML
file containing normalized SVG path strings for every key icon used in
the emulator's on-screen keypad.

USAGE
    python3 tools/regen_svg_paths.py > qml/SvgPaths.qml

INPUT
    g_elements.jsonl  — one JSON object per line, each representing a
                        top-level <g> element from the SVG.  Fields:
                          id:       e.g. "NSPIRE_KEY_A_NONE_NONE"
                          children: array of child SVG elements
                        Each child has: tag, class, and tag-specific
                        attributes (d for <path>, points for <polygon>,
                        x/y/width/height for <rect>).

OUTPUT
    A QML file (stdout) defining a QtObject with one readonly property
    per icon.  Each property is a string of the form:

        "D<normW>,<normH> <SVG path data>"

    where normW/normH encode the normalized bounding-box dimensions so
    the renderer (SvgIcon.qml) can apply correct uniform scaling while
    preserving the icon's aspect ratio.

NORMALIZATION STRATEGY
    Every SVG path is translated so its bounding box starts at (0,0),
    then scaled by a single uniform factor.  There are two modes:

    1. Per-glyph normalization (normalize_uniform):
       scale = 1 / max(width, height)
       Used for standalone symbols with no visual size relationship.

    2. Group normalization (normalize_group):
       scale = 1 / group_max_dim
       All members of a group share the same scale factor (derived from
       the largest bounding-box dimension across the group).  This
       ensures, e.g., all letters A-Z render at the same cap-height,
       and all digits 0-9 have consistent sizing.

       The path is centered horizontally within a virtual [0, 1.0] wide
       box, so the D-prefix is always "D1.0000,<normH>".

    Groups defined:
    - LETTER group:   A-Z — share letter_max (width of W, the widest)
    - NUMPAD group:   0-9, decimal (.), comma (,) — share number_max
    - OPERATOR group: + − × ÷ = ^ space — also use number_max so
                      they're proportionally sized relative to digits.
                      This makes thin symbols (minus, equal, space)
                      render thin instead of stretching to fill the
                      full button width.

ARCHITECTURE NOTE
    The D-prefix is parsed at runtime by SvgIcon.qml, which computes:
        fitScale = min(drawW / normW, drawH / normH)
    This guarantees the icon fits within the available area with correct
    aspect ratio and is centered both horizontally and vertically.
"""

import json
import re
import sys
import os

# ---------------------------------------------------------------------------
#  SVG path tokenizer and bounding-box tracer
# ---------------------------------------------------------------------------

def tokenize(d):
    """Split an SVG path data string into command letters and numbers.

    Handles all standard SVG path commands (M/m L/l H/h V/v C/c S/s
    Q/q T/t A/a Z/z) and numeric values including negative signs,
    decimals, and scientific notation.
    """
    return re.findall(
        r'[MmLlHhVvCcSsQqTtAaZz]'           # command letters
        r'|[-+]?(?:\d+\.?\d*|\.\d+)'          # numbers (int or float)
        r'(?:[eE][-+]?\d+)?',                  # optional exponent
        d
    )


def trace_bbox(d):
    """Compute the axis-aligned bounding box of an SVG path.

    Walks through all path commands, tracking the current point and
    recording every explicitly visited coordinate.  Returns
    (minX, minY, maxX, maxY) or None if the path has no coordinates.

    NOTE: For cubic/quadratic curves, this records control points too,
    which gives an approximate (slightly enlarged) bounding box.  This
    is acceptable for our normalization purposes.
    """
    xs, ys = [], []
    cx, cy = 0.0, 0.0          # current point
    sx, sy = 0.0, 0.0          # subpath start (for Z command)
    tokens = tokenize(d)
    i, cmd = 0, 'M'

    while i < len(tokens):
        t = tokens[i]
        if t.isalpha() or t in ('Z', 'z'):
            cmd = t
            i += 1
            if cmd in ('Z', 'z'):
                cx, cy = sx, sy
            continue

        # --- Absolute commands ---
        if cmd == 'M':
            cx, cy = float(tokens[i]), float(tokens[i+1])
            sx, sy = cx, cy
            xs.append(cx); ys.append(cy)
            i += 2
            cmd = 'L'  # implicit lineto after moveto
        elif cmd == 'L':
            cx, cy = float(tokens[i]), float(tokens[i+1])
            xs.append(cx); ys.append(cy)
            i += 2
        elif cmd == 'H':
            cx = float(tokens[i])
            xs.append(cx)
            i += 1
        elif cmd == 'V':
            cy = float(tokens[i])
            ys.append(cy)
            i += 1
        elif cmd == 'C':       # cubic Bézier (3 pairs)
            for _ in range(3):
                px, py = float(tokens[i]), float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            cx, cy = px, py
        elif cmd == 'S':       # smooth cubic (2 pairs)
            for _ in range(2):
                px, py = float(tokens[i]), float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            cx, cy = px, py
        elif cmd == 'Q':       # quadratic Bézier (2 pairs)
            for _ in range(2):
                px, py = float(tokens[i]), float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            cx, cy = px, py
        elif cmd == 'T':       # smooth quadratic (1 pair)
            cx, cy = float(tokens[i]), float(tokens[i+1])
            xs.append(cx); ys.append(cy)
            i += 2
        elif cmd == 'A':       # arc (7 params: rx ry rot large sweep x y)
            # Only record the endpoint for bbox purposes
            px, py = float(tokens[i+5]), float(tokens[i+6])
            xs.append(px); ys.append(py)
            cx, cy = px, py
            i += 7

        # --- Relative commands ---
        elif cmd == 'm':
            cx += float(tokens[i]); cy += float(tokens[i+1])
            sx, sy = cx, cy
            xs.append(cx); ys.append(cy)
            i += 2
            cmd = 'l'
        elif cmd == 'l':
            cx += float(tokens[i]); cy += float(tokens[i+1])
            xs.append(cx); ys.append(cy)
            i += 2
        elif cmd == 'h':
            cx += float(tokens[i])
            xs.append(cx)
            i += 1
        elif cmd == 'v':
            cy += float(tokens[i])
            ys.append(cy)
            i += 1
        elif cmd == 'c':       # relative cubic (3 pairs of offsets)
            for _ in range(3):
                px = cx + float(tokens[i])
                py = cy + float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            cx, cy = px, py
        elif cmd == 's':       # relative smooth cubic (2 pairs)
            for _ in range(2):
                px = cx + float(tokens[i])
                py = cy + float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            # NOTE: don't update cx,cy here since the loop already
            # computed absolute positions for all points, but px,py
            # from the last iteration is the endpoint
            cx, cy = px, py
        elif cmd == 'q':       # relative quadratic (2 pairs)
            for _ in range(2):
                px = cx + float(tokens[i])
                py = cy + float(tokens[i+1])
                xs.append(px); ys.append(py)
                i += 2
            cx, cy = px, py
        elif cmd == 't':       # relative smooth quadratic
            cx += float(tokens[i]); cy += float(tokens[i+1])
            xs.append(cx); ys.append(cy)
            i += 2
        elif cmd == 'a':       # relative arc
            px = cx + float(tokens[i+5])
            py = cy + float(tokens[i+6])
            xs.append(px); ys.append(py)
            cx, cy = px, py
            i += 7
        else:
            i += 1  # skip unknown token

    if not xs or not ys:
        return None
    return min(xs), min(ys), max(xs), max(ys)


# ---------------------------------------------------------------------------
#  Path normalization
# ---------------------------------------------------------------------------

def _transform_path(d, minx, miny, scale, offset_x=0.0):
    """Apply translate-then-scale to all coordinates in an SVG path.

    Absolute coordinates are transformed as:
        x' = (x - minx) * scale + offset_x
        y' = (y - miny) * scale
    Relative coordinates (deltas) are just scaled:
        dx' = dx * scale
        dy' = dy * scale
    Arc radii are also scaled uniformly.

    Returns the transformed path as a string.
    """
    tokens = tokenize(d)
    result = []
    i, cmd = 0, 'M'

    def tx(x):  return (x - minx) * scale + offset_x
    def ty(y):  return (y - miny) * scale
    def tdx(dx): return dx * scale
    def tdy(dy): return dy * scale
    def fmt(v): return f"{v:.4f}"

    while i < len(tokens):
        t = tokens[i]
        if t.isalpha() or t in ('Z', 'z'):
            cmd = t
            result.append(cmd)
            i += 1
            if cmd in ('Z', 'z'):
                continue

        # Absolute commands
        if cmd in ('M', 'L', 'T'):
            x, y = float(tokens[i]), float(tokens[i+1])
            result.append(f"{fmt(tx(x))},{fmt(ty(y))}")
            i += 2
            if cmd == 'M':
                cmd = 'L'  # subsequent coords are implicit lineto
        elif cmd == 'H':
            x = float(tokens[i])
            result.append(fmt(tx(x)))
            i += 1
        elif cmd == 'V':
            y = float(tokens[i])
            result.append(fmt(ty(y)))
            i += 1
        elif cmd == 'C':       # 3 control/end points
            for _ in range(3):
                x, y = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tx(x))},{fmt(ty(y))}")
                i += 2
        elif cmd == 'S':       # 2 points
            for _ in range(2):
                x, y = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tx(x))},{fmt(ty(y))}")
                i += 2
        elif cmd == 'Q':       # 2 points
            for _ in range(2):
                x, y = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tx(x))},{fmt(ty(y))}")
                i += 2
        elif cmd == 'A':       # arc: rx ry rotation large-arc sweep x y
            rx, ry = float(tokens[i]), float(tokens[i+1])
            rot, la, sw = tokens[i+2], tokens[i+3], tokens[i+4]
            x, y = float(tokens[i+5]), float(tokens[i+6])
            result.append(f"{fmt(rx * scale)},{fmt(ry * scale)}")
            result.append(rot); result.append(la); result.append(sw)
            result.append(f"{fmt(tx(x))},{fmt(ty(y))}")
            i += 7

        # Relative commands
        elif cmd in ('m', 'l', 't'):
            dx, dy = float(tokens[i]), float(tokens[i+1])
            result.append(f"{fmt(tdx(dx))},{fmt(tdy(dy))}")
            i += 2
            if cmd == 'm':
                cmd = 'l'
        elif cmd == 'h':
            dx = float(tokens[i])
            result.append(fmt(tdx(dx)))
            i += 1
        elif cmd == 'v':
            dy = float(tokens[i])
            result.append(fmt(tdy(dy)))
            i += 1
        elif cmd == 'c':
            for _ in range(3):
                dx, dy = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tdx(dx))},{fmt(tdy(dy))}")
                i += 2
        elif cmd == 's':
            for _ in range(2):
                dx, dy = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tdx(dx))},{fmt(tdy(dy))}")
                i += 2
        elif cmd == 'q':
            for _ in range(2):
                dx, dy = float(tokens[i]), float(tokens[i+1])
                result.append(f"{fmt(tdx(dx))},{fmt(tdy(dy))}")
                i += 2
        elif cmd == 'a':
            rx, ry = float(tokens[i]), float(tokens[i+1])
            rot, la, sw = tokens[i+2], tokens[i+3], tokens[i+4]
            dx, dy = float(tokens[i+5]), float(tokens[i+6])
            result.append(f"{fmt(rx * scale)},{fmt(ry * scale)}")
            result.append(rot); result.append(la); result.append(sw)
            result.append(f"{fmt(tdx(dx))},{fmt(tdy(dy))}")
            i += 7
        else:
            result.append(tokens[i])
            i += 1

    return ' '.join(result)


def normalize_uniform(d):
    """Normalize a path using per-glyph uniform scaling.

    The path is translated to origin and scaled so that
    max(width, height) = 1.0.  Returns a D-prefixed string:
        "D<normW>,<normH> <path data>"
    where normW, normH are the normalized dimensions
    (one of them is 1.0, the other <= 1.0).

    Returns None if the path has no valid bounding box.
    """
    bbox = trace_bbox(d)
    if bbox is None:
        return None
    minx, miny, maxx, maxy = bbox
    w = maxx - minx
    h = maxy - miny
    if w < 0.001 and h < 0.001:
        return None
    if w < 0.001: w = 0.001
    if h < 0.001: h = 0.001

    max_dim = max(w, h)
    scale = 1.0 / max_dim
    normW = w * scale    # one of normW/normH will be 1.0
    normH = h * scale

    path_str = _transform_path(d, minx, miny, scale)
    return f"D{normW:.4f},{normH:.4f} {path_str}"


def normalize_group(d, group_max_dim):
    """Normalize a path using a shared group scale factor.

    All members of a visual group (e.g. letters A-Z, digits 0-9, or
    operators) are scaled by the SAME factor: 1 / group_max_dim.  This
    ensures consistent relative sizing across the group.

    The path is centered horizontally within a virtual 1.0-wide box,
    so the D-prefix is always "D1.0000,<normH>".  At render time,
    SvgIcon computes fitScale = min(drawW/1.0, drawH/normH), which
    means the virtual 1.0 width bounds the horizontal scale, and the
    path sits centered within that space.

    Args:
        d: Raw SVG path data string.
        group_max_dim: The largest bounding-box dimension across all
                       members of this group (e.g., the height of "0"
                       for the digit group).

    Returns:
        D-prefixed normalized path string, or None on error.
    """
    bbox = trace_bbox(d)
    if bbox is None:
        return None
    minx, miny, maxx, maxy = bbox
    w = maxx - minx
    h = maxy - miny
    if w < 0.001 and h < 0.001:
        return None
    if w < 0.001: w = 0.001
    if h < 0.001: h = 0.001

    scale = 1.0 / group_max_dim
    normW = w * scale       # < 1.0 for glyphs narrower than group max
    normH = h * scale       # < 1.0 for glyphs shorter than group max

    # Center the glyph horizontally within the [0, 1.0] virtual box
    offset_x = (1.0 - normW) / 2.0

    path_str = _transform_path(d, minx, miny, scale, offset_x)
    return f"D{1.0:.4f},{normH:.4f} {path_str}"


# ---------------------------------------------------------------------------
#  SVG element helpers
# ---------------------------------------------------------------------------

def polygon_to_path(pts_str):
    """Convert an SVG <polygon points="..."> to a path data string.

    Example: "10,20 30,40 50,60" → "M10,20 L30,40 L50,60 Z"
    """
    pts = pts_str.strip().split()
    nums = []
    for p in pts:
        for n in p.split(','):
            n = n.strip()
            if n:
                nums.append(n)
    parts = []
    for j in range(0, len(nums), 2):
        if j + 1 < len(nums):
            cmd = 'M' if j == 0 else 'L'
            parts.append(f"{cmd}{nums[j]},{nums[j+1]}")
    parts.append('Z')
    return ' '.join(parts)


def rect_to_path(x, y, w, h):
    """Convert an SVG <rect> to a closed path data string."""
    x, y, w, h = float(x), float(y), float(w), float(h)
    return f"M{x},{y} L{x+w},{y} L{x+w},{y+h} L{x},{y+h} Z"


def combine_elements(elements):
    """Combine multiple SVG child elements into one path data string.

    Handles <path>, <polygon>, and <rect> elements.  The paths are
    concatenated with spaces, forming a single compound path.
    """
    paths = []
    for el in elements:
        tag = el.get('tag', '')
        if tag == 'path':
            d = el.get('d', '')
            if d:
                paths.append(d.strip())
        elif tag == 'polygon':
            pts = el.get('points', '')
            if pts:
                paths.append(polygon_to_path(pts))
        elif tag == 'rect':
            paths.append(rect_to_path(
                el.get('x', '0'), el.get('y', '0'),
                el.get('width', '0'), el.get('height', '0')
            ))
    return ' '.join(paths)


def extract(children, cls):
    """Return child elements matching a specific CSS class."""
    return [c for c in children if c.get('class') == cls]


# ---------------------------------------------------------------------------
#  Key → property mappings
# ---------------------------------------------------------------------------
#
# Each entry maps a key ID (from g_elements.jsonl) to:
#   (QML_property_name, CSS_class_of_child_elements)
#
# The CSS class determines which child SVG elements to extract:
#   - ti_generalKey_art      → primary artwork for general keys
#   - ti_numpadKey_art       → primary artwork for numpad keys
#   - ti_secondKey_art       → primary artwork for ctrl/shift keys
#   - ti_generalKey_art_second   → secondary (blue) label
#   - ti_numpadKey_art_second    → secondary label for numpad keys
#   - ti_generalTouchpad_art     → touchpad directional arrows

PRIMARY = {
    # --- Letters A-Z (ti_generalKey_art, except X/Y/Z on numpad) ---
    "NSPIRE_KEY_A_NONE_NONE": ("letterA", "ti_generalKey_art"),
    "NSPIRE_KEY_B_NONE_NONE": ("letterB", "ti_generalKey_art"),
    "NSPIRE_KEY_C_NONE_NONE": ("letterC", "ti_generalKey_art"),
    "NSPIRE_KEY_D_NONE_NONE": ("letterD", "ti_generalKey_art"),
    "NSPIRE_KEY_E_NONE_NONE": ("letterE", "ti_generalKey_art"),
    "NSPIRE_KEY_F_NONE_NONE": ("letterF", "ti_generalKey_art"),
    "NSPIRE_KEY_G_NONE_NONE": ("letterG", "ti_generalKey_art"),
    "NSPIRE_KEY_H_NONE_NONE": ("letterH", "ti_generalKey_art"),
    "NSPIRE_KEY_I_NONE_NONE": ("letterI", "ti_generalKey_art"),
    "NSPIRE_KEY_J_NONE_NONE": ("letterJ", "ti_generalKey_art"),
    "NSPIRE_KEY_K_NONE_NONE": ("letterK", "ti_generalKey_art"),
    "NSPIRE_KEY_L_NONE_NONE": ("letterL", "ti_generalKey_art"),
    "NSPIRE_KEY_M_NONE_NONE": ("letterM", "ti_generalKey_art"),
    "NSPIRE_KEY_N_NONE_NONE": ("letterN", "ti_generalKey_art"),
    "NSPIRE_KEY_O_NONE_NONE": ("letterO", "ti_generalKey_art"),
    "NSPIRE_KEY_P_NONE_NONE": ("letterP", "ti_generalKey_art"),
    "NSPIRE_KEY_Q_NONE_NONE": ("letterQ", "ti_generalKey_art"),
    "NSPIRE_KEY_R_NONE_NONE": ("letterR", "ti_generalKey_art"),
    "NSPIRE_KEY_S_NONE_NONE": ("letterS", "ti_generalKey_art"),
    "NSPIRE_KEY_T_NONE_NONE": ("letterT", "ti_generalKey_art"),
    "NSPIRE_KEY_U_NONE_NONE": ("letterU", "ti_generalKey_art"),
    "NSPIRE_KEY_V_NONE_NONE": ("letterV", "ti_generalKey_art"),
    "NSPIRE_KEY_W_NONE_NONE": ("letterW", "ti_generalKey_art"),
    "NSPIRE_KEY_X_NONE_NONE": ("letterX", "ti_numpadKey_art"),
    "NSPIRE_KEY_Y_NONE_NONE": ("letterY", "ti_numpadKey_art"),
    "NSPIRE_KEY_Z_NONE_NONE": ("letterZ", "ti_numpadKey_art"),

    # --- Digits 0-9 ---
    "NSPIRE_KEY_ZERO_NONE_NONE": ("num0", "ti_numpadKey_art"),
    "NSPIRE_KEY_ONE_END_NONE": ("num1", "ti_numpadKey_art"),
    "NSPIRE_KEY_TWO_NONE_NONE": ("num2", "ti_numpadKey_art"),
    "NSPIRE_KEY_THREE_PAGEDOWN_NONE": ("num3", "ti_numpadKey_art"),
    "NSPIRE_KEY_FOUR_GROUP_NONE": ("num4", "ti_numpadKey_art"),
    "NSPIRE_KEY_FIVE_NONE_NONE": ("num5", "ti_numpadKey_art"),
    "NSPIRE_KEY_SIX_UNGROUP_NONE": ("num6", "ti_numpadKey_art"),
    "NSPIRE_KEY_SEVEN_BEGINNING_NONE": ("num7", "ti_numpadKey_art"),
    "NSPIRE_KEY_EIGHT_NONE_NONE": ("num8", "ti_numpadKey_art"),
    "NSPIRE_KEY_NINE_PAGEUP_NONE": ("num9", "ti_numpadKey_art"),

    # --- Arithmetic operators ---
    "NSPIRE_KEY_PLUS_CONTRASTUP_INTEGRATION": ("plus", "ti_generalKey_art"),
    "NSPIRE_KEY_MINUS_CONTRASTDOWN_DERIVATIVE": ("minus", "ti_generalKey_art"),
    "NSPIRE_KEY_MULTIPLY_MATHBOX_NONE": ("multiply", "ti_generalKey_art"),
    "NSPIRE_KEY_DIVIDE_FRACTIONTEMPLATE_BACKSLASH": ("divide", "ti_generalKey_art"),
    "NSPIRE_KEY_EQUAL_INEQUALITYPOPUP_NONE": ("equal", "ti_generalKey_art"),
    "NSPIRE_KEY_CARET_NTHROOT_NONE": ("caret", "ti_generalKey_art"),

    # --- Math / symbol keys ---
    "NSPIRE_KEY_XSQUARED_SQRT_NONE": ("xSquared", "ti_generalKey_art"),
    "NSPIRE_KEY_ETOTHEX_LN_NONE": ("eToTheX", "ti_generalKey_art"),
    "NSPIRE_KEY_TENTOTHEX_LOG_NONE": ("tenToTheX", "ti_generalKey_art"),
    "NSPIRE_KEY_PARENLEFT_BRACKETS_NONE": ("parenLeft", "ti_generalKey_art"),
    "NSPIRE_KEY_PARENRIGHT_BRACES_NONE": ("parenRight", "ti_generalKey_art"),

    # --- Numpad extras ---
    "NSPIRE_KEY_DECIMAL_DATASAMPLETRIGGER_NONE": ("decimal", "ti_numpadKey_art"),
    "NSPIRE_KEY_NEGATIVE_ANS_NONE": ("negative", "ti_numpadKey_art"),
    "NSPIRE_KEY_COMMA_NONE_NONE": ("comma", "ti_generalKey_art"),
    "NSPIRE_KEY_EE_NONE_NONE": ("ee", "ti_generalKey_art"),
    "NSPIRE_KEY_PIPOPUP_NONE_NONE": ("pi", "ti_generalKey_art"),
    "NSPIRE_KEY_PUNCTUATIONPOPUP_HINTS_NONE": ("punctuation", "ti_generalKey_art"),
    "NSPIRE_KEY_SPACE_UNDERSCORE_NONE": ("space", "ti_generalKey_art"),
    "NSPIRE_KEY_RETURN_NONE_COLUMNTEMPLATE": ("returnKey", "ti_generalKey_art"),
    "NSPIRE_KEY_FLAGPOPUP_TEMPLATEPALETTE_NONE": ("flag", "ti_generalKey_art"),

    # --- Function / navigation keys ---
    "NSPIRE_KEY_TRIGPOPUP_HINTS_NONE": ("trig", "ti_generalKey_art"),
    "NSPIRE_KEY_CATALOG_SYMBOLTEMPLATE_NONE": ("catalog", "ti_generalKey_art"),
    "NSPIRE_KEY_TEMPLATE_DEFINE_NONE": ("templates", "ti_generalKey_art"),
    "NSPIRE_KEY_CTRL_NONE_NONE": ("ctrl", "ti_secondKey_art"),
    "NSPIRE_KEY_TAB_ZONECHANGE_NONE": ("tab", "ti_generalKey_art"),
    "NSPIRE_KEY_ENTER_APPROXIMATE_NONE": ("enter", "ti_generalKey_art"),
    "NSPIRE_KEY_ESC_UNDO_REDO": ("esc", "ti_generalKey_art"),
    "NSPIRE_KEY_DOC_ADDPAGE_NONE": ("doc", "ti_generalKey_art"),
    "NSPIRE_KEY_SCRATCHPAD_SAVE_NONE": ("scratchpad", "ti_generalKey_art"),
    "NSPIRE_KEY_MENU_CONTEXTMENU_NONE": ("menu", "ti_generalKey_art"),
    "NSPIRE_KEY_HOME_PAGEBAR_OFF_NONE": ("home", "ti_generalKey_art"),
    "NSPIRE_KEY_VAR_STORE_NONE": ("varKey", "ti_generalKey_art"),
    "NSPIRE_KEY_BACKSPACE_CLEAR_NONE": ("del", "ti_generalKey_art"),
    "NSPIRE_KEY_SHIFT_CAPS_NONE": ("shift", "ti_generalKey_art"),
}

# Secondary (blue) label artwork — shown above keys when shift is active
SECONDARY = {
    "NSPIRE_KEY_ENTER_APPROXIMATE_NONE": ("approximate", "ti_generalKey_art_second"),
    "NSPIRE_KEY_PARENRIGHT_BRACES_NONE": ("braces", "ti_generalKey_art_second"),
    "NSPIRE_KEY_PARENLEFT_BRACKETS_NONE": ("brackets", "ti_generalKey_art_second"),
    "NSPIRE_KEY_TENTOTHEX_LOG_NONE": ("log", "ti_generalKey_art_second"),
    "NSPIRE_KEY_ETOTHEX_LN_NONE": ("ln", "ti_generalKey_art_second"),
    "NSPIRE_KEY_DIVIDE_FRACTIONTEMPLATE_BACKSLASH": ("fractionTemplate", "ti_generalKey_art_second"),
    "NSPIRE_KEY_MULTIPLY_MATHBOX_NONE": ("mathBox", "ti_generalKey_art_second"),
    "NSPIRE_KEY_XSQUARED_SQRT_NONE": ("sqrt", "ti_generalKey_art_second"),
    "NSPIRE_KEY_CARET_NTHROOT_NONE": ("nthRoot", "ti_generalKey_art_second"),
    "NSPIRE_KEY_CATALOG_SYMBOLTEMPLATE_NONE": ("symbolTemplate", "ti_generalKey_art_second"),
    "NSPIRE_KEY_TEMPLATE_DEFINE_NONE": ("define", "ti_generalKey_art_second"),
    "NSPIRE_KEY_TRIGPOPUP_HINTS_NONE": ("hintsQuestion", "ti_generalKey_art_second"),
    "NSPIRE_KEY_EQUAL_INEQUALITYPOPUP_NONE": ("inequality", "ti_generalKey_art_second"),
    "NSPIRE_KEY_BACKSPACE_CLEAR_NONE": ("clear", "ti_generalKey_art_second"),
    "NSPIRE_KEY_VAR_STORE_NONE": ("store", "ti_generalKey_art_second"),
    "NSPIRE_KEY_SHIFT_CAPS_NONE": ("caps", "ti_generalKey_art_second"),
    "NSPIRE_KEY_MENU_CONTEXTMENU_NONE": ("contextMenu", "ti_generalKey_art_second"),
    "NSPIRE_KEY_DOC_ADDPAGE_NONE": ("addPage", "ti_generalKey_art_second"),
    "NSPIRE_KEY_SCRATCHPAD_SAVE_NONE": ("save", "ti_generalKey_art_second"),
    "NSPIRE_KEY_HOME_PAGEBAR_OFF_NONE": ("pageBarOff", "ti_generalKey_art_second"),
    "NSPIRE_KEY_ESC_UNDO_REDO": ("undo", "ti_generalKey_art_second"),
    "NSPIRE_KEY_NEGATIVE_ANS_NONE": ("ans", "ti_numpadKey_art_second"),
    "NSPIRE_KEY_DECIMAL_DATASAMPLETRIGGER_NONE": ("capture", "ti_numpadKey_art_second"),
    # These two keys have split secondary art: a contrast arrow + a
    # calculus symbol (integral/derivative circle).  We split them into
    # separate properties in the extraction code below.
    "NSPIRE_KEY_MINUS_CONTRASTDOWN_DERIVATIVE": ("contrastDown_derivative", "ti_generalKey_art_second"),
    "NSPIRE_KEY_PLUS_CONTRASTUP_INTEGRATION": ("contrastUp_integration", "ti_generalKey_art_second"),
}

# Touchpad directional arrows (the blue secondary icons at touchpad edges)
TOUCHPAD = {
    "NSPIRE_KEY_UP_VIEWUP_NONE": "touchpadUp",
    "NSPIRE_KEY_DOWN_VIEWDOWN_NONE": "touchpadDown",
    "NSPIRE_KEY_LEFT_PREVPAGE_NONE": "touchpadLeft",
    "NSPIRE_KEY_RIGHT_NEXTPAGE_NONE": "touchpadRight",
}

# ---------------------------------------------------------------------------
#  Group definitions for consistent sizing
# ---------------------------------------------------------------------------
#
# Within each group, all glyphs share the same normalization scale factor
# (1 / group_max_dim).  This ensures visual consistency:
#   - All letters appear at the same cap-height
#   - All digits appear at the same height
#   - Operators sized relative to digits (thin symbols stay thin)

# Letters A-Z: group_max_dim = width of W (the widest letter ≈ 11.89px)
LETTER_PROPS = {f'letter{c}' for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'}

# Digits 0-9 plus decimal point and comma: group_max_dim from tallest
# digit (≈ 14.44px).  This makes the decimal point render as a small
# dot (only ~3px tall vs 14.44px max → normH ≈ 0.23), and comma as a
# small mark, proportionally correct relative to digits.
NUMPAD_PROPS = {f'num{d}' for d in '0123456789'} | {'decimal', 'comma'}

# Arithmetic operators and space bar: also normalized using the digit
# group_max_dim (14.44px).  This ensures thin symbols like minus
# (2.26px tall) render proportionally thin (normH ≈ 0.16) rather than
# being stretched to fill their button.  Without this, minus would
# appear as a thick bar filling the full button width.
OPERATOR_PROPS = {'plus', 'minus', 'multiply', 'divide', 'equal', 'caret', 'space'}


# ---------------------------------------------------------------------------
#  Desired output order in the QML file
# ---------------------------------------------------------------------------

OUTPUT_ORDER = [
    # Letters
    'letterA', 'letterB', 'letterC', 'letterD', 'letterE', 'letterF',
    'letterG', 'letterH', 'letterI', 'letterJ', 'letterK', 'letterL',
    'letterM', 'letterN', 'letterO', 'letterP', 'letterQ', 'letterR',
    'letterS', 'letterT', 'letterU', 'letterV', 'letterW', 'letterX',
    'letterY', 'letterZ',
    # Numbers
    'num0', 'num1', 'num2', 'num3', 'num4', 'num5', 'num6', 'num7',
    'num8', 'num9',
    # Operators
    'plus', 'minus', 'multiply', 'divide', 'equal', 'caret',
    # Key symbols
    'xSquared', 'eToTheX', 'tenToTheX', 'parenLeft', 'parenRight',
    'decimal', 'negative', 'comma', 'ee', 'pi', 'punctuation',
    'space', 'returnKey', 'flag',
    # Function keys
    'shift', 'ctrl', 'tab', 'esc', 'enter', 'doc', 'scratchpad',
    'menu', 'home', 'varKey', 'del', 'trig', 'catalog', 'templates',
    # Secondary art
    'approximate', 'braces', 'brackets', 'log', 'ln', 'fractionTemplate',
    'mathBox', 'sqrt', 'nthRoot', 'symbolTemplate', 'define',
    'hintsQuestion', 'inequality', 'clear', 'store', 'caps',
    'contextMenu', 'addPage', 'save', 'pageBarOff', 'undo', 'ans',
    'capture',
    # Contrast / calculus symbols
    'contrastUp', 'contrastDown', 'integrationCircle', 'derivativeCircle',
    # Arrow triangles (manually defined, not from SVG)
    'arrowUp', 'arrowDown', 'arrowLeft', 'arrowRight',
    # Del arrow overlay
    'delArrow',
    # Touchpad icons
    'touchpadGrab', 'touchpadUp', 'touchpadDown', 'touchpadLeft',
    'touchpadRight', 'touchpadCenter',
]


# ---------------------------------------------------------------------------
#  Main: load data, normalize, emit QML
# ---------------------------------------------------------------------------

def compute_group_max(raw_paths, prop_set):
    """Find the largest bounding-box dimension across a set of properties."""
    max_d = 0.0
    for prop in prop_set:
        if prop not in raw_paths:
            continue
        bbox = trace_bbox(raw_paths[prop])
        if bbox:
            w = bbox[2] - bbox[0]
            h = bbox[3] - bbox[1]
            max_d = max(max_d, w, h)
    return max_d


def main():
    # Resolve path to g_elements.jsonl relative to this script's location,
    # or fall back to current directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    jsonl_path = os.path.join(project_root, 'g_elements.jsonl')
    if not os.path.exists(jsonl_path):
        jsonl_path = 'g_elements.jsonl'

    # Load all key element data
    data = {}
    with open(jsonl_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            data[obj['id']] = obj

    # ---- Phase 1: Extract raw (un-normalized) paths for primary art ----

    raw_paths = {}  # prop_name → raw combined SVG path string
    for kid, (prop, cls) in PRIMARY.items():
        if kid not in data:
            continue
        children = data[kid]['children']
        els = extract(children, cls)
        if not els:
            continue
        raw_paths[prop] = combine_elements(els)

    # ---- Phase 2: Compute group maximum dimensions ----

    letter_max = compute_group_max(raw_paths, LETTER_PROPS)
    # For numpad and operators, we use the NUMBER max (digit height ≈ 14.44px)
    # so that all numpad-area symbols are proportionally consistent.
    # Operators are included in the same scale reference because they share
    # the numpad visual context and thin operators (minus, equal) should
    # render thin relative to digit height.
    number_max = compute_group_max(raw_paths, {f'num{d}' for d in '0123456789'})

    print(f"// letter_max = {letter_max:.2f}, number_max = {number_max:.2f}",
          file=sys.stderr)

    # ---- Phase 3: Normalize all primary paths ----

    results = {}  # prop_name → normalized D-prefixed path string

    for prop, raw in raw_paths.items():
        if prop in LETTER_PROPS and letter_max > 0:
            norm = normalize_group(raw, letter_max)
        elif prop in NUMPAD_PROPS and number_max > 0:
            norm = normalize_group(raw, number_max)
        elif prop in OPERATOR_PROPS and number_max > 0:
            norm = normalize_group(raw, number_max)
        else:
            norm = normalize_uniform(raw)
        if norm:
            results[prop] = norm

    # ---- Phase 4: Extract and normalize secondary art ----

    for kid, (prop, cls) in SECONDARY.items():
        if kid not in data:
            continue
        children = data[kid]['children']

        # Special case: contrast+calculus keys have two visual elements
        # packed into one secondary art group.  We split them:
        #   Element 0 = contrast arrow (▲ or ▼)
        #   Element 1 = calculus symbol (∫ or ∂ circle)
        if prop in ('contrastDown_derivative', 'contrastUp_integration'):
            els = extract(children, cls)
            if len(els) >= 2:
                arrow = combine_elements([els[0]])
                arrow_n = normalize_uniform(arrow)
                circle = combine_elements([els[1]])
                circle_n = normalize_uniform(circle)
                if prop == 'contrastDown_derivative':
                    if arrow_n: results['contrastDown'] = arrow_n
                    if circle_n: results['derivativeCircle'] = circle_n
                else:
                    if arrow_n: results['contrastUp'] = arrow_n
                    if circle_n: results['integrationCircle'] = circle_n
            continue

        els = extract(children, cls)
        if not els:
            continue
        combined = combine_elements(els)
        norm = normalize_uniform(combined)
        if norm:
            results[prop] = norm

    # ---- Phase 5: Extract del arrow (special: third child element) ----
    # The backspace/del key has its arrow as child index 2 (a polygon)
    kid = "NSPIRE_KEY_BACKSPACE_CLEAR_NONE"
    if kid in data:
        children = data[kid]['children']
        if len(children) > 2:
            arrow = combine_elements([children[2]])
            an = normalize_uniform(arrow)
            if an:
                results['delArrow'] = an

    # ---- Phase 6: Manually defined arrow triangles ----
    # These are simple equilateral-ish triangles for the small arrow
    # indicators between the touchpad edges and center grab icon.
    # They don't come from the SVG; they're hand-coded 1:1 aspect ratio
    # triangles pointing in each cardinal direction.
    results['arrowUp']    = "D1.0000,1.0000 M 0.5000,0.0000 L 1.0000,1.0000 L 0.0000,1.0000 Z"
    results['arrowDown']  = "D1.0000,1.0000 M 0.5000,1.0000 L 0.0000,0.0000 L 1.0000,0.0000 Z"
    results['arrowLeft']  = "D1.0000,1.0000 M 0.0000,0.5000 L 1.0000,0.0000 L 1.0000,1.0000 Z"
    results['arrowRight'] = "D1.0000,1.0000 M 1.0000,0.5000 L 0.0000,0.0000 L 0.0000,1.0000 Z"

    # ---- Phase 7: Touchpad directional icons ----
    # These are the blue secondary icons at the touchpad edges (e.g., the
    # "view up" icon shown at the top of the touchpad).
    for kid, prop in TOUCHPAD.items():
        if kid not in data:
            continue
        children = data[kid]['children']
        els = extract(children, 'ti_generalTouchpad_art')
        if els:
            combined = combine_elements(els)
            norm = normalize_uniform(combined)
            if norm:
                results[prop] = norm

    # Touchpad center: grab icon and hand icon
    kid = "NSPIRE_KEY_CENTERCLICK_GRAB_NONE"
    if kid in data:
        children = data[kid]['children']
        grab_els = extract(children, 'ti_generalKey_art')
        if grab_els:
            combined = combine_elements(grab_els)
            norm = normalize_uniform(combined)
            if norm:
                results['touchpadGrab'] = norm
        tp_els = extract(children, 'ti_generalTouchpad_art')
        if tp_els:
            combined = combine_elements(tp_els)
            norm = normalize_uniform(combined)
            if norm:
                results['touchpadCenter'] = norm

    # ---- Phase 8: Emit QML ----

    print('import QtQuick 6.0')
    print()
    print('QtObject {')

    for prop in OUTPUT_ORDER:
        if prop in results:
            print(f'    readonly property string {prop}: "{results[prop]}"')
        else:
            print(f'    // {prop}: not found in source data')

    # Catch any properties we generated but forgot to list in OUTPUT_ORDER
    extra = set(results.keys()) - set(OUTPUT_ORDER)
    if extra:
        print()
        print('    // === Extra properties (not in OUTPUT_ORDER) ===')
        for prop in sorted(extra):
            print(f'    readonly property string {prop}: "{results[prop]}"')

    print('}')


if __name__ == '__main__':
    main()
