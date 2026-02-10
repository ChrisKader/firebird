#!/usr/bin/env python3
"""
Generate a QCAD-compatible DXF drawing of the TI-Nspire CX II CAS calculator.

All dimensions are in millimetres, matching render_calc.py exactly.
Layers are used to organise elements by type.

Output: tools/ti_nspire_cx2.dxf
"""

import ezdxf
from ezdxf import enums
import math
import os

# =====================================================================
# MASTER DIMENSIONS (mm) — copied from render_calc.py
# =====================================================================
CALC_W      = 87.3          # total width
GLASS_H     = 102.30         # glass panel height
KEYPAD_H    = 87.30          # keypad panel height
CALC_PAD_H  = 0.9
CALC_H      = GLASS_H + KEYPAD_H + CALC_PAD_H   # total height = 190.50 mm
BLUE_H      = 190.26        # blue shell height (measured)

BEZEL_SIDE  = 6.0           # blue border width each side
INNER_W     = 75.5          # inner panel width
INNER_X     = BEZEL_SIDE    # left edge of inner panel
INNER_R     = INNER_X + INNER_W   # right edge of inner panel

SHELL_R     = 18.0          # shell/mask bottom corner radius
KP_R        = 3.5           # keypad bottom corner radius

BTN_MARGIN  = 2.5           # buttons inset from inner edges

# Screen
SCR_W       = 65.0
SCR_H       = 50.0
SCR_X       = INNER_X + (INNER_W - SCR_W) / 2
SCR_Y_OFF   = 15.8          # from glass top

# Trackpad
TP_W        = 32.0
TP_H        = 24.0
TP_HOLE_M   = 0.5           # hole margin around trackpad
TP_OL_GAP   = 0.25          # outline gap from hole
TP_OL_THICK = 0.9           # outline thickness
TP_HOLE_BTM = 4.7           # hole bottom from glass bottom
CB_SIZE     = 10.0          # click button size

# Button sizes
NUM_W       = 9.8
NUM_H       = 6.1
DUAL_PAIR   = 14.14
DUAL_DIV    = 1.0
ENTER_W     = 14.14
ALPHA_SZ    = 4.48
SMALL_W     = 6.38
SMALL_H     = 4.48
SPACE_W     = 12.0
SPACE_H     = 4.8
BTN_INSET   = 0.20

# Gaps
HG          = 2.9            # number button h-gap
VG          = 3.3            # number/dual button v-gap
HGA         = 3.0            # alpha h-gap
VGA         = 2.4            # alpha v-gap
GAP_DG      = 2.8            # dual-to-number gap

# Side buttons
SIDE_W      = 9.8
SIDE_H      = 6.1
SIDE_GAP    = 4.10
SIDE_BTM    = 3.5            # bottom button hole bottom from glass bottom

# Corner radii
BTN_R       = 1.05           # standard button corner radius
ALPHA_R     = 0.6            # alpha button corner radius
TP_OUTLINE_R = 5.5
TP_HOLE_R   = 4.5
TP_SURF_R   = 3.5
CB_R        = 2.0

# =====================================================================
# COORDINATE SYSTEM
# =====================================================================
# We place the calculator with bottom-left at (0, 0), Y going up.
# All render_calc.py coordinates are top-left origin, Y down.
# Convert: dxf_y = CALC_H - render_y

def flip_y(y_from_top):
    """Convert render_calc.py Y (top-down) to DXF Y (bottom-up)."""
    return CALC_H - y_from_top


# =====================================================================
# COMPUTED LAYOUT (same logic as render_calc.py)
# =====================================================================
# Vertical positions (from top, render_calc coords)
GLASS_Y_TOP  = 0.0
KEYPAD_Y_TOP = GLASS_H
SHELL_BTM    = GLASS_H + KEYPAD_H
BLUE_BTM     = BLUE_H

# Button grid positions
KP_L = INNER_X + BTN_MARGIN
KP_R_edge = INNER_R - BTN_MARGIN
RDUAL_X = KP_R_edge - DUAL_PAIR

# Number grid: centred between dual columns
_num_grid_w = 3 * NUM_W + 2 * HG
_between_duals = RDUAL_X - (KP_L + DUAL_PAIR)
_num_gap = (_between_duals - _num_grid_w) / 2
GRID_L = KP_L + DUAL_PAIR + _num_gap
GRID_COL = [GRID_L + i * (NUM_W + HG) for i in range(3)]

# Row positions (from top)
R0_Y = KEYPAD_Y_TOP + 4.0   # ctrl row
R1_Y = R0_Y + NUM_H + VG    # first dual/number row

# Enter button
ENTER_Y = R1_Y + 3 * (NUM_H + VG)

# Alpha rows
ALPHA_Y0 = R1_Y + 4 * (NUM_H + VG) + 1.5  # 1.5mm extra gap

# Alpha column positions
EE_X = KP_L
A_X  = KP_L + DUAL_PAIR - ALPHA_SZ
G_X  = RDUAL_X
QM_X = RDUAL_X + ENTER_W - SMALL_W

# B-F evenly distributed
_bf_space = G_X - (A_X + ALPHA_SZ)
_bf_gap   = (_bf_space - 5 * ALPHA_SZ) / 6

ALPHA_COL_X = [EE_X, A_X]
for k in range(5):
    ALPHA_COL_X.append(A_X + ALPHA_SZ + _bf_gap + k * (ALPHA_SZ + _bf_gap))
ALPHA_COL_X.append(G_X)
ALPHA_COL_X.append(QM_X)

# Side buttons
GLASS_BTM_Y = GLASS_H   # from top
BOT_BTN_TOP = GLASS_BTM_Y - SIDE_BTM - SIDE_H

# Trackpad
HOLE_W = TP_W + 2 * TP_HOLE_M
HOLE_H = TP_H + 2 * TP_HOLE_M
HOLE_X = INNER_X + (INNER_W - HOLE_W) / 2
HOLE_Y_TOP = GLASS_BTM_Y - TP_HOLE_BTM - HOLE_H
TP_X = HOLE_X + TP_HOLE_M
TP_Y_TOP = HOLE_Y_TOP + TP_HOLE_M

OL_TOTAL_W = HOLE_W + 2 * (TP_OL_GAP + TP_OL_THICK)
OL_TOTAL_H = HOLE_H + 2 * (TP_OL_GAP + TP_OL_THICK)
OL_X = HOLE_X - TP_OL_GAP - TP_OL_THICK
OL_Y_TOP = HOLE_Y_TOP - TP_OL_GAP - TP_OL_THICK


# =====================================================================
# DXF SETUP
# =====================================================================
doc = ezdxf.new('R2010')
msp = doc.modelspace()

# Create layers
LAYERS = {
    'OUTLINE':       {'color': 7},     # white — shell outline + mask
    'BLUE_SHELL':    {'color': 4},     # cyan — blue shell border
    'GLASS':         {'color': 1},     # red — glass panel
    'KEYPAD':        {'color': 3},     # green — keypad panel
    'SCREEN':        {'color': 2},     # yellow — LCD screen
    'TRACKPAD':      {'color': 5},     # magenta — trackpad elements
    'BUTTONS_HOLE':  {'color': 8},     # dark grey — button holes
    'BUTTONS_CAP':   {'color': 7},     # white — button caps
    'SIDE_BUTTONS':  {'color': 6},     # yellow — side buttons on glass
    'TEXT':          {'color': 7},     # white — labels
    'DIMENSIONS':    {'color': 3},     # green — dimension annotations
    'CENTERLINES':   {'color': 1},     # red — centre lines
}

for name, props in LAYERS.items():
    doc.layers.add(name, color=props['color'])


# =====================================================================
# DRAWING HELPERS
# =====================================================================
def rounded_rect(x_left, y_top, w, h, r, layer, close=True):
    """Draw a rounded rectangle.
    x_left, y_top are in render_calc coordinates (top-left origin).
    Converts to DXF bottom-left origin.
    """
    # Clamp radius to half the smallest dimension
    r = min(r, w / 2, h / 2)

    # DXF coords: bottom-left origin
    x1 = x_left
    x2 = x_left + w
    y1 = flip_y(y_top + h)   # bottom in DXF
    y2 = flip_y(y_top)       # top in DXF

    if r < 0.01:
        # Simple rectangle
        pts = [(x1, y1), (x2, y1), (x2, y2), (x1, y2), (x1, y1)]
        msp.add_lwpolyline(pts, dxfattribs={'layer': layer})
        return

    # Lines: bottom, right, top, left
    msp.add_line((x1 + r, y1), (x2 - r, y1), dxfattribs={'layer': layer})  # bottom
    msp.add_line((x2, y1 + r), (x2, y2 - r), dxfattribs={'layer': layer})  # right
    msp.add_line((x2 - r, y2), (x1 + r, y2), dxfattribs={'layer': layer})  # top
    msp.add_line((x1, y2 - r), (x1, y1 + r), dxfattribs={'layer': layer})  # left

    # Corner arcs (centre, radius, start_angle, end_angle in degrees)
    # Bottom-left: 180 to 270
    msp.add_arc((x1 + r, y1 + r), r, 180, 270, dxfattribs={'layer': layer})
    # Bottom-right: 270 to 360
    msp.add_arc((x2 - r, y1 + r), r, 270, 360, dxfattribs={'layer': layer})
    # Top-right: 0 to 90
    msp.add_arc((x2 - r, y2 - r), r, 0, 90, dxfattribs={'layer': layer})
    # Top-left: 90 to 180
    msp.add_arc((x1 + r, y2 - r), r, 90, 180, dxfattribs={'layer': layer})


def rounded_rect_bottom_only(x_left, y_top, w, h, r, layer):
    """Draw a rectangle with only the bottom corners rounded (top is square).
    Used for shell outline, blue shell, keypad.
    """
    r = min(r, w / 2, h / 2)

    x1 = x_left
    x2 = x_left + w
    y1 = flip_y(y_top + h)   # bottom in DXF
    y2 = flip_y(y_top)       # top in DXF

    # Top edge (square)
    msp.add_line((x1, y2), (x2, y2), dxfattribs={'layer': layer})
    # Right side
    msp.add_line((x2, y2), (x2, y1 + r), dxfattribs={'layer': layer})
    # Bottom edge
    msp.add_line((x2 - r, y1), (x1 + r, y1), dxfattribs={'layer': layer})
    # Left side
    msp.add_line((x1, y1 + r), (x1, y2), dxfattribs={'layer': layer})

    if r >= 0.01:
        # Bottom-left arc: 180 to 270
        msp.add_arc((x1 + r, y1 + r), r, 180, 270, dxfattribs={'layer': layer})
        # Bottom-right arc: 270 to 360
        msp.add_arc((x2 - r, y1 + r), r, 270, 360, dxfattribs={'layer': layer})


def add_text(x_centre, y_top_render, text, height, layer):
    """Add centred text at a position (render_calc coords)."""
    dxf_y = flip_y(y_top_render) - height / 2
    msp.add_text(
        text,
        height=height,
        dxfattribs={
            'layer': layer,
            'halign': enums.TextHAlign.CENTER,
            'valign': enums.TextVAlign.MIDDLE,
            'insert': (x_centre, dxf_y),
        }
    ).set_placement((x_centre, dxf_y), align=enums.TextEntityAlignment.MIDDLE_CENTER)


def btn_hole(x, y, w, h, r, layer='BUTTONS_HOLE'):
    """Draw a button hole outline."""
    rounded_rect(x, y, w, h, r, layer)


def btn_cap(x, y, w, h, r, layer='BUTTONS_CAP'):
    """Draw a button cap outline (inset from hole)."""
    i = BTN_INSET
    rounded_rect(x + i, y + i, w - 2*i, h - 2*i, max(0.1, r - i), layer)


def draw_button(x, y, w, h, label, r=BTN_R, text_h=1.8):
    """Draw a complete button: hole + cap + label."""
    btn_hole(x, y, w, h, r)
    btn_cap(x, y, w, h, r)
    if label:
        add_text(x + w/2, y + h/2, label, text_h, 'TEXT')


def draw_dual_button(x, y, t1, t2, s1="", s2=""):
    """Draw a dual button: one hole, two caps."""
    r = BTN_R
    hw = (DUAL_PAIR - DUAL_DIV) / 2

    # Single long hole
    btn_hole(x, y, DUAL_PAIR, NUM_H, r)

    # Left cap
    i = BTN_INSET
    rounded_rect(x + i, y + i, hw - 2*i, NUM_H - 2*i, max(0.1, r - i), 'BUTTONS_CAP')

    # Right cap
    rx = x + hw + DUAL_DIV
    rounded_rect(rx + i, y + i, hw - 2*i, NUM_H - 2*i, max(0.1, r - i), 'BUTTONS_CAP')

    # Divider line
    dx = x + hw
    msp.add_line(
        (dx + DUAL_DIV / 2, flip_y(y + 0.3)),
        (dx + DUAL_DIV / 2, flip_y(y + NUM_H - 0.3)),
        dxfattribs={'layer': 'BUTTONS_HOLE'}
    )

    # Labels
    if t1:
        add_text(x + hw / 2, y + NUM_H / 2, t1, 1.5, 'TEXT')
    if t2:
        add_text(rx + hw / 2, y + NUM_H / 2, t2, 1.5, 'TEXT')

    # Secondary labels above
    if s1:
        add_text(x + hw / 2, y - 1.5, s1, 1.0, 'TEXT')
    if s2:
        add_text(rx + hw / 2, y - 1.5, s2, 1.0, 'TEXT')


# =====================================================================
# DRAW: SHELL OUTLINE (alpha mask shape)
# =====================================================================
rounded_rect_bottom_only(0, 0, CALC_W, CALC_H, SHELL_R, 'OUTLINE')

# =====================================================================
# DRAW: BLUE SHELL (full-width, rounded bottom)
# =====================================================================
rounded_rect_bottom_only(0, 0, CALC_W, BLUE_H, SHELL_R, 'BLUE_SHELL')

# =====================================================================
# DRAW: GLASS PANEL (inner rectangle)
# =====================================================================
rounded_rect(INNER_X, 0, INNER_W, GLASS_H, 0, 'GLASS')

# =====================================================================
# DRAW: KEYPAD PANEL (inner, rounded bottom corners)
# =====================================================================
rounded_rect_bottom_only(INNER_X, GLASS_H, INNER_W, KEYPAD_H, KP_R, 'KEYPAD')

# =====================================================================
# DRAW: SCREEN
# =====================================================================
rounded_rect(SCR_X, SCR_Y_OFF, SCR_W, SCR_H, 0, 'SCREEN')

# =====================================================================
# DRAW: TRACKPAD
# =====================================================================
# White outline
rounded_rect(OL_X, OL_Y_TOP, OL_TOTAL_W, OL_TOTAL_H, TP_OUTLINE_R, 'TRACKPAD')

# Hole
rounded_rect(HOLE_X, HOLE_Y_TOP, HOLE_W, HOLE_H, TP_HOLE_R, 'TRACKPAD')

# Surface
rounded_rect(TP_X, TP_Y_TOP, TP_W, TP_H, TP_SURF_R, 'TRACKPAD')

# Bezel (inset by outline thickness)
bz = TP_OL_THICK
rounded_rect(TP_X + bz, TP_Y_TOP + bz, TP_W - 2*bz, TP_H - 2*bz, 3.0, 'TRACKPAD')

# Click button
cb_x = TP_X + (TP_W - CB_SIZE) / 2
cb_y = TP_Y_TOP + (TP_H - CB_SIZE) / 2
rounded_rect(cb_x, cb_y, CB_SIZE, CB_SIZE, CB_R, 'TRACKPAD')

# Directional arrows (small triangles as polylines)
mid_x = TP_X + TP_W / 2
mid_y_top = TP_Y_TOP + TP_H / 2  # in render coords (from top)
a = 0.535   # arrow half-size
ad = 2.25   # distance from edge

# Up arrow
uy = TP_Y_TOP + ad
pts = [(mid_x, flip_y(uy)), (mid_x - a, flip_y(uy + a)), (mid_x + a, flip_y(uy + a)), (mid_x, flip_y(uy))]
msp.add_lwpolyline(pts, dxfattribs={'layer': 'TRACKPAD'})

# Down arrow
dy = TP_Y_TOP + TP_H - ad
pts = [(mid_x, flip_y(dy)), (mid_x - a, flip_y(dy - a)), (mid_x + a, flip_y(dy - a)), (mid_x, flip_y(dy))]
msp.add_lwpolyline(pts, dxfattribs={'layer': 'TRACKPAD'})

# Left arrow
lx = TP_X + ad
pts = [(lx, flip_y(mid_y_top)), (lx + a, flip_y(mid_y_top - a)), (lx + a, flip_y(mid_y_top + a)), (lx, flip_y(mid_y_top))]
msp.add_lwpolyline(pts, dxfattribs={'layer': 'TRACKPAD'})

# Right arrow
rx_arr = TP_X + TP_W - ad
pts = [(rx_arr, flip_y(mid_y_top)), (rx_arr - a, flip_y(mid_y_top - a)), (rx_arr - a, flip_y(mid_y_top + a)), (rx_arr, flip_y(mid_y_top))]
msp.add_lwpolyline(pts, dxfattribs={'layer': 'TRACKPAD'})

# =====================================================================
# DRAW: SIDE BUTTONS (3 per side on glass)
# =====================================================================
left_x  = INNER_X + BTN_MARGIN
right_x = INNER_R - BTN_MARGIN - SIDE_W

left_labels  = ["esc", "pad", "tab"]
right_labels = ["on", "doc", "menu"]

for i in range(3):
    by = BOT_BTN_TOP - (2 - i) * (SIDE_H + SIDE_GAP)
    draw_button(left_x, by, SIDE_W, SIDE_H, left_labels[i], text_h=1.5)
    draw_button(right_x, by, SIDE_W, SIDE_H, right_labels[i], text_h=1.5)

# =====================================================================
# DRAW: CTRL ROW (ctrl, shift, var, del)
# =====================================================================
draw_button(KP_L, R0_Y, NUM_W, NUM_H, "ctrl", text_h=1.5)
draw_button(GRID_COL[0], R0_Y, NUM_W, NUM_H, "shift", text_h=1.5)
draw_button(GRID_COL[2], R0_Y, NUM_W, NUM_H, "var", text_h=1.5)
draw_button(RDUAL_X + DUAL_PAIR - NUM_W, R0_Y, NUM_W, NUM_H, "del", text_h=1.5)

# =====================================================================
# DRAW: DUAL BUTTONS (rows 1-4)
# =====================================================================
left_duals = [
    ("=",    "trig"),
    ("^",    "x2"),
    ("ex",   "10x"),
    ("(",    ")"),
]
right_duals = [
    ("tmpl", "cat"),
    ("x",    "/"),
    ("+",    "-"),
]

for i, (t1, t2) in enumerate(left_duals):
    ry = R1_Y + i * (NUM_H + VG)
    draw_dual_button(KP_L, ry, t1, t2)

for i, (t1, t2) in enumerate(right_duals):
    ry = R1_Y + i * (NUM_H + VG)
    draw_dual_button(RDUAL_X, ry, t1, t2)

# Enter button
draw_button(RDUAL_X, ENTER_Y, ENTER_W, NUM_H, "enter", text_h=1.5)

# =====================================================================
# DRAW: NUMBER GRID (4 rows x 3 columns)
# =====================================================================
nums = [["7","8","9"], ["4","5","6"], ["1","2","3"], ["0",".","(-)"]]
for ri, row in enumerate(nums):
    for ci, label in enumerate(row):
        nx = GRID_COL[ci]
        ny = R1_Y + ri * (NUM_H + VG)
        draw_button(nx, ny, NUM_W, NUM_H, label, text_h=1.5)

# =====================================================================
# DRAW: ALPHA ROWS
# =====================================================================
alpha_rows = [
    # Row 0: EE A B C D E F G ?!
    [("EE",1),("A",0),("B",0),("C",0),("D",0),("E",0),("F",0),("G",0),("?!",1)],
    # Row 1: pi> H I J K L M N flag
    [("pi",1),("H",0),("I",0),("J",0),("K",0),("L",0),("M",0),("N",0),("flg",1)],
    # Row 2: , O P Q R S T U ret
    [(",",1),("O",0),("P",0),("Q",0),("R",0),("S",0),("T",0),("U",0),("ret",1)],
]

for row_i, row in enumerate(alpha_rows):
    ay = ALPHA_Y0 + row_i * (ALPHA_SZ + VGA)
    for j, (lbl, is_small) in enumerate(row):
        bw = SMALL_W if is_small else ALPHA_SZ
        bh = SMALL_H if is_small else ALPHA_SZ
        draw_button(ALPHA_COL_X[j], ay, bw, bh, lbl, r=ALPHA_R, text_h=1.2)

# Row 3: V W X Y Z [space]
ay = ALPHA_Y0 + 3 * (ALPHA_SZ + VGA)
for k, lbl in enumerate(["V", "W", "X", "Y", "Z"]):
    draw_button(ALPHA_COL_X[1 + k], ay, ALPHA_SZ, ALPHA_SZ, lbl, r=ALPHA_R, text_h=1.2)

# Space bar
space_x = ALPHA_COL_X[6]
draw_button(space_x, ay, SPACE_W, SPACE_H, "", r=ALPHA_R, text_h=1.2)
# Underline indicator
msp.add_line(
    (space_x + 2.0, flip_y(ay + SPACE_H - 1.2)),
    (space_x + SPACE_W - 2.0, flip_y(ay + SPACE_H - 1.2)),
    dxfattribs={'layer': 'TEXT'}
)

# =====================================================================
# DRAW: CENTRE LINES
# =====================================================================
cx = CALC_W / 2
# Vertical centreline (full height)
msp.add_line((cx, -5), (cx, CALC_H + 5), dxfattribs={'layer': 'CENTERLINES'})
# Horizontal centreline at glass/keypad boundary
msp.add_line((-5, flip_y(GLASS_H)), (CALC_W + 5, flip_y(GLASS_H)),
             dxfattribs={'layer': 'CENTERLINES'})

# =====================================================================
# DRAW: KEY DIMENSIONS
# =====================================================================
dim_style = doc.dimstyles.new('CALCDIM')
dim_style.dxf.dimtxt = 1.5      # text height
dim_style.dxf.dimasz = 1.0      # arrow size
dim_style.dxf.dimexo = 0.5      # extension line offset
dim_style.dxf.dimexe = 0.5      # extension beyond dim line
dim_style.dxf.dimgap = 0.3      # text gap

DIM_LAYER = 'DIMENSIONS'
DIM_OFFSET = 5.0   # how far from the object to place dimension lines

# Overall width
msp.add_linear_dim(
    base=(0, flip_y(0) + DIM_OFFSET),
    p1=(0, flip_y(0)),
    p2=(CALC_W, flip_y(0)),
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Overall height (left side)
msp.add_linear_dim(
    base=(-DIM_OFFSET, 0),
    p1=(0, flip_y(0)),
    p2=(0, flip_y(CALC_H)),
    angle=90,
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Glass height (right side)
msp.add_linear_dim(
    base=(CALC_W + DIM_OFFSET, 0),
    p1=(CALC_W, flip_y(0)),
    p2=(CALC_W, flip_y(GLASS_H)),
    angle=90,
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Keypad height (right side)
msp.add_linear_dim(
    base=(CALC_W + DIM_OFFSET + 5, 0),
    p1=(CALC_W, flip_y(GLASS_H)),
    p2=(CALC_W, flip_y(CALC_H)),
    angle=90,
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Bezel width (left)
msp.add_linear_dim(
    base=(0, flip_y(GLASS_H / 2)),
    p1=(0, flip_y(GLASS_H / 2)),
    p2=(INNER_X, flip_y(GLASS_H / 2)),
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Inner panel width
msp.add_linear_dim(
    base=(INNER_X, flip_y(0) + DIM_OFFSET + 5),
    p1=(INNER_X, flip_y(0)),
    p2=(INNER_R, flip_y(0)),
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Screen dimensions
msp.add_linear_dim(
    base=(SCR_X, flip_y(SCR_Y_OFF) + 3),
    p1=(SCR_X, flip_y(SCR_Y_OFF)),
    p2=(SCR_X + SCR_W, flip_y(SCR_Y_OFF)),
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

msp.add_linear_dim(
    base=(SCR_X - 3, flip_y(SCR_Y_OFF)),
    p1=(SCR_X, flip_y(SCR_Y_OFF)),
    p2=(SCR_X, flip_y(SCR_Y_OFF + SCR_H)),
    angle=90,
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# Trackpad dimensions
msp.add_linear_dim(
    base=(TP_X, flip_y(TP_Y_TOP) + 3),
    p1=(TP_X, flip_y(TP_Y_TOP)),
    p2=(TP_X + TP_W, flip_y(TP_Y_TOP)),
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

msp.add_linear_dim(
    base=(TP_X - 3, flip_y(TP_Y_TOP)),
    p1=(TP_X, flip_y(TP_Y_TOP)),
    p2=(TP_X, flip_y(TP_Y_TOP + TP_H)),
    angle=90,
    dimstyle='CALCDIM',
    dxfattribs={'layer': DIM_LAYER}
).render()

# =====================================================================
# SAVE
# =====================================================================
out_dir  = os.path.dirname(os.path.abspath(__file__))
out_path = os.path.join(out_dir, 'ti_nspire_cx2.dxf')
doc.saveas(out_path)
print(f"Saved DXF to {out_path}")
print(f"  Units: millimetres (1 DXF unit = 1 mm)")
print(f"  Calculator: {CALC_W} x {CALC_H} mm")
print(f"  Layers: {', '.join(LAYERS.keys())}")
print(f"\nOpen in QCAD: File > Open > {out_path}")
