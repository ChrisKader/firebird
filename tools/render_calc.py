#!/usr/bin/env python3
"""
Render the TI-Nspire CX II CAS calculator front view.

All dimensions come from real caliper measurements expressed in mm,
converted to pixels via the mm() helper.

LAYER STACK (drawn back-to-front):
  1. BODY_BLACK      — Full-canvas dark rectangle.  Visible at the
                       bottom corners below the blue shell where the
                       keypad curves inward.
  2. BLUE_SHELL      — Full-width polygon from y=0 to blue_bottom
                       (187.26 mm) with R=10 mm rounded bottom corners.
                       This is the outer plastic shell.
  3. GLASS           — Dark rectangle over the inner-panel centre
                       (inner_x..inner_r) for the top 102.3 mm.
                       Hides blue in the centre so only side borders
                       remain visible.
  4. KEYPAD (PLASTIC) — Dark polygon over the inner-panel centre with
                       R=8 mm rounded bottom corners reaching all the
                       way to shell_bottom (189.7 mm).
  5. Buttons / UI    — All button, label, touchpad, screen drawing.
  6. ALPHA MASK      — Clips the outer image corners to the shell
                       silhouette (square top, R=10 mm rounded bottom)
                       using transparency.

Output: tools/ti_nspire_render.png
"""

from PIL import Image, ImageDraw, ImageFont
import math
import os

# =====================================================================
# IMAGE SIZE & MASTER SCALE
# =====================================================================
# Physical calculator width = 87.5 mm.  We render at 798 px wide.
W = 798                        # image width  (px)
S = W / 87.5                   # scale factor: ~9.12 px/mm

# Total height = glass (102.3) + keypad (87.4) = 189.7 mm + 1 px top padding
H = round(189.7 * S) + 1       # image height (px, ~1731)

def mm(v):
    """Convert a measurement in millimetres to pixels."""
    return round(v * S)

# The physical calculator's bottom corners are rounded with ~10 mm
# radius.  Used for: blue shell polygon, alpha mask.
shell_corner_r = mm(18.0)      # outer-shell bottom-corner radius (px)

# =====================================================================
# IMAGE CREATION (RGBA for final alpha-mask rounding)
# =====================================================================
BLUE_SHELL = '#0099D8'         # blue outer-shell colour
img  = Image.new('RGBA', (W, H), (255, 255, 255, 255))  # white canvas
draw = ImageDraw.Draw(img)

# =====================================================================
# FONTS
# =====================================================================
def load_font(size):
    """Try common system font paths; fall back to Pillow default."""
    for path in ["/System/Library/Fonts/Helvetica.ttc",        # macOS
                 "/System/Library/Fonts/SFNSMono.ttf",         # macOS
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try:
            return ImageFont.truetype(path, size)
        except (OSError, IOError):
            continue
    return ImageFont.load_default()

font_logo    = load_font(38)        # "TI-nspire CX II" / "CAS" branding
font_logo_sm = load_font(mm(2.2))   # subtitle (reserved)
font_btn     = load_font(mm(4.375)) # standard button labels
font_btn_sm  = load_font(mm(3.625)) # smaller labels (side btns, ctrl row)
font_alpha   = load_font(mm(3.375)) # alpha letter buttons (A–Z)
font_sec     = load_font(mm(1.6))   # secondary (blue) labels above buttons
font_screen  = load_font(mm(2.0))   # on-screen text (reserved)

# =====================================================================
# COLOURS
# =====================================================================
GLASS      = '#1a1a1e'   # dark glass-panel background (screen area)
PLASTIC    = '#28282c'   # dark keypad-panel background (button area)
BTN        = '#38383c'   # standard button fill
BTN_NUM    = '#444448'   # number button fill (slightly lighter)
BTN_CTRL   = '#1565C0'   # ctrl button fill (blue accent)
BTN_BORDER = '#505055'   # button outline
TXT        = '#e8e8e8'   # primary button-label colour
TXT_SEC    = '#00aadd'   # secondary (shift) label colour
TXT_MUTED  = '#666666'   # muted text (touchpad arrows, space-bar line)
SCREEN_BG  = '#00cc44'   # LCD background (green for debug visibility)
TP_BG      = '#222226'   # touchpad active-area background
TP_BORDER  = '#444448'   # touchpad outer border
DIVIDER    = '#1a1a1a'   # divider between dual-button halves
BTN_HOLE   = '#1a1a1e'   # button hole (recess behind the button cap)

# =====================================================================
# LAYOUT CONSTANTS — horizontal
# =====================================================================
# Cross-section (left to right):
#   |<- bezel 6 mm ->|<--- inner panel 75.5 mm --->|<- bezel 6 mm ->|
#   |<-------------------- 87.5 mm total ----------------------->|
bezel_side = mm(6.0)           # blue border width on each side
inner_w    = mm(75.5)          # inner-panel width (glass & keypad)
inner_x    = bezel_side        # left  x of inner panel (6 mm)
inner_r    = inner_x + inner_w # right x of inner panel (81.5 mm)
btn_margin = mm(2.5)           # buttons inset 2.5 mm from inner edges

# =====================================================================
# LAYOUT CONSTANTS — vertical
# =====================================================================
# Top to bottom:
#   |<-- glass 102.3 mm -->|<-- keypad 87.4 mm -->|
#   |<-------------- 189.7 mm total ------------->|
#
# The blue border is measured at 187.26 mm — the bottom 2.44 mm of the
# calculator shows the dark body + keypad only (no blue).
glass_h  = mm(102.3)          # glass-panel height
keypad_h = mm(87.4)           # keypad-panel height
blue_h   = mm(190.26)         # blue-border height (measured 187.26 + 3 mm)

glass_y      = 1                       # 1 px top padding
keypad_y     = glass_y + glass_h       # keypad top (~102.3 mm)
shell_bottom = keypad_y + keypad_h     # calculator bottom (~189.7 mm)
blue_bottom  = glass_y + blue_h        # blue shell ends here (187.26 mm + padding)

# =====================================================================
# LAYER 1 — BODY (dark background, full canvas)
# =====================================================================
# The dark body of the calculator.  Mostly covered by the blue shell
# and inner panels, but peeks through at the bottom corners where the
# keypad curves inward while the blue has already ended (below 187.26 mm).
BODY_BLACK = '#1B1D20'
draw.rectangle([0, glass_y, W, H], fill=BODY_BLACK)

# =====================================================================
# LAYER 2 — BLUE SHELL (full-width polygon, rounded bottom corners)
# =====================================================================
# Spans x=0..W, y=0..blue_bottom (187.26 mm).  The bottom edge has
# R = shell_corner_r (10 mm) rounded corners.
#
# Shape traced as a polygon:
#
#   (0,0) ────────────────────── (W,0)        square top
#     │                               │
#     │        straight sides         │
#     │                               │
#   (0, yb-R)                  (W, yb-R)     arc starts here
#      ╲                            ╱
#       ╲   quarter-circle arcs    ╱          R = 10 mm
#        (R, yb) ────── (W-R, yb)            flat bottom
#
#   yb = blue_bottom = 187.26 mm
#   R  = shell_corner_r = 10 mm
#
# The glass and keypad drawn ON TOP hide the blue in the centre,
# leaving only the thin side borders (0..inner_x and inner_r..W)
# visible as the blue shell.
n_arc_shell = 120              # points per quarter-circle (smooth)
r  = shell_corner_r            # shorthand for this section
yb = blue_bottom               # shorthand: blue-shell bottom y

shell_pts = []

# Top-left → top-right
shell_pts.append((0, glass_y))
shell_pts.append((W, glass_y))

# Right side down to where the bottom-right arc begins
shell_pts.append((W, yb - r))

# Bottom-right arc: theta 0 → pi/2  (right → down)
# Arc centre at (W - R, yb - R)
cx, cy = W - r, yb - r
for i in range(n_arc_shell + 1):
    t = (math.pi / 2) * i / n_arc_shell
    shell_pts.append((cx + r * math.cos(t),
                      cy + r * math.sin(t)))

# Flat bottom between the two arcs
shell_pts.append((r, yb))

# Bottom-left arc: theta pi/2 → pi  (down → left)
# Arc centre at (R, yb - R)
cx, cy = r, yb - r
for i in range(n_arc_shell + 1):
    t = math.pi / 2 + (math.pi / 2) * i / n_arc_shell
    shell_pts.append((cx + r * math.cos(t),
                      cy + r * math.sin(t)))

# Left side back up to top-left (closes polygon)
shell_pts.append((0, yb - r))
shell_pts.append((0, glass_y))

draw.polygon(shell_pts, fill=BLUE_SHELL)

# =====================================================================
# LAYER 3 — GLASS PANEL
# =====================================================================
# Dark rectangle covering the centre of the blue shell in the glass
# region.  Only the blue side borders remain visible.
draw.rectangle([inner_x, glass_y, inner_r, glass_y + glass_h], fill=GLASS)

# =====================================================================
# LAYER 4 — KEYPAD PANEL (dark polygon, rounded bottom corners)
# =====================================================================
# Fills the inner panel from keypad_y to shell_bottom with R = 8 mm
# rounded bottom corners.  Hides remaining blue in the centre below
# the glass.
#
# Shape:
#   (inner_x, keypad_y) ──────── (inner_r, keypad_y)   square top
#     │                                          │
#     │           straight sides                 │
#     │                                          │
#   (inner_x, cy_kp)                (inner_r, cy_kp)   arc starts
#      ╲                                      ╱
#       ╲  quarter-circle arcs (R = 8 mm)    ╱
#        (cx_l, shell_bot) ── (cx_r, shell_bot)        flat bottom
#
#   kp_corner_r = 8 mm   (keypad's own bottom-corner radius)
#   cy_kp  = shell_bottom - kp_corner_r  (where straight edge → arc)
#   cx_l   = inner_x + kp_corner_r       (left  arc centre X)
#   cx_r   = inner_r - kp_corner_r       (right arc centre X)
kp_corner_r = mm(3.5)         # keypad bottom-corner radius (small so inner blue edge stays straight)
n_arc       = 80               # points per quarter-circle

cx_l_kp = inner_x + kp_corner_r    # left  arc centre X
cx_r_kp = inner_r - kp_corner_r    # right arc centre X
cy_kp   = shell_bottom - kp_corner_r  # both arcs' centre Y

kp_pts = []

# Top edge (square corners, flush with keypad_y)
kp_pts.append((inner_x, keypad_y))
kp_pts.append((inner_r, keypad_y))

# Right side: straight down to where the arc starts
kp_pts.append((inner_r, cy_kp))

# Bottom-right arc: theta 0 (right) → pi/2 (down)
for i in range(1, n_arc + 1):
    theta = (math.pi / 2) * i / n_arc
    px = cx_r_kp + kp_corner_r * math.cos(theta)
    py = cy_kp   + kp_corner_r * math.sin(theta)
    kp_pts.append((round(px), round(py)))

# Flat bottom between the two arcs (y = shell_bottom)
kp_pts.append((cx_l_kp, shell_bottom))

# Bottom-left arc: theta pi/2 (down) → pi (left)
for i in range(1, n_arc + 1):
    theta = math.pi / 2 + (math.pi / 2) * i / n_arc
    px = cx_l_kp + kp_corner_r * math.cos(theta)
    py = cy_kp   + kp_corner_r * math.sin(theta)
    kp_pts.append((round(px), round(py)))

# Left side: auto-closes back to (inner_x, keypad_y)
draw.polygon(kp_pts, fill=PLASTIC)

# =====================================================================
# SCREEN — position and dimensions
# =====================================================================
scr_w = mm(65)                 # LCD width
scr_h = mm(50)                 # LCD height
scr_x = inner_x + (inner_w - scr_w) // 2   # centred horizontally
scr_y = glass_y + mm(15.8)    # 15.8 mm below glass top (below logo)

# =====================================================================
# LOGO — "TI-nspire CX II" and "CAS"
# =====================================================================
# Sits between glass top and screen top.
# "TI-nspire CX II" centred over the screen width.
# "CAS" right-aligned to the screen's right edge.
logo_text = "TI-nspire CX II"
logo_bbox = draw.textbbox((0, 0), logo_text, font=font_logo)
logo_tw   = logo_bbox[2] - logo_bbox[0]    # text width (px)

cas_text = "CAS"
cas_bbox = draw.textbbox((0, 0), cas_text, font=font_logo)
cas_tw   = cas_bbox[2] - cas_bbox[0]

logo_y0 = glass_y + mm(3.15)  # 3.15 mm from glass top
draw.text((scr_x + (scr_w - logo_tw) // 2, logo_y0),
          logo_text, fill='#b0b0b0', font=font_logo)
draw.text((scr_x + scr_w - cas_tw, logo_y0),
          cas_text, fill='#b0b0b0', font=font_logo)

# =====================================================================
# SCREEN (LCD) — solid green for debug visibility
# =====================================================================
draw.rectangle([scr_x, scr_y, scr_x + scr_w, scr_y + scr_h], fill=SCREEN_BG)

# =====================================================================
# TOUCHPAD — centred below the screen
# =====================================================================
tp_w = mm(32)                  # trackpad surface width
tp_h = mm(24)                  # trackpad surface height

# Hole in the glass is 0.5 mm larger than the trackpad on each side
tp_hole_margin = mm(0.5)
hole_w = tp_w + 2 * tp_hole_margin
hole_h = tp_h + 2 * tp_hole_margin

# White outline on glass: 1 mm thick, 0.25 mm outside the hole edge
tp_outline_gap   = mm(0.25)    # gap between hole edge and outline
tp_outline_thick = mm(0.9)     # outline stroke width

# Position: trackpad hole bottom is 5.5 mm from glass bottom
glass_bottom = glass_y + glass_h
hole_x = inner_x + (inner_w - hole_w) // 2            # centred horizontally
hole_y = glass_bottom - mm(4.7) - hole_h               # hole top y

# Derive trackpad surface and outline from hole position
tp_x = hole_x + tp_hole_margin
tp_y = hole_y + tp_hole_margin
outline_total_w = hole_w + 2 * (tp_outline_gap + tp_outline_thick)
outline_total_h = hole_h + 2 * (tp_outline_gap + tp_outline_thick)
outline_x = hole_x - tp_outline_gap - tp_outline_thick
outline_y = hole_y - tp_outline_gap - tp_outline_thick

# White outline (drawn on the glass, rounded rect)
draw.rounded_rectangle([outline_x, outline_y,
                        outline_x + outline_total_w, outline_y + outline_total_h],
                       radius=mm(5.5), outline='#cccccc', width=round(tp_outline_thick))

# Hole (dark recess behind the trackpad)
draw.rounded_rectangle([hole_x, hole_y, hole_x + hole_w, hole_y + hole_h],
                       radius=mm(4.5), fill='#111114')

# Trackpad surface (centred in hole)
draw.rounded_rectangle([tp_x, tp_y, tp_x + tp_w, tp_y + tp_h],
                       radius=mm(3.5), fill=TP_BG)

# Bezel: inset border inside the trackpad surface, same distance as white outline width
bz = tp_outline_thick          # bezel inset = outline thickness (0.9 mm)
draw.rounded_rectangle([tp_x + bz, tp_y + bz,
                        tp_x + tp_w - bz, tp_y + tp_h - bz],
                       radius=mm(3.0), outline='#3a3a3e', width=max(1, round(mm(0.15))))

# Centre click button (square, rounded corners)
cb   = mm(10.0)                # click-button side length
cb_x = tp_x + (tp_w - cb) // 2
cb_y = tp_y + (tp_h - cb) // 2
draw.rounded_rectangle([cb_x, cb_y, cb_x + cb, cb_y + cb],
                       radius=mm(2.0), fill=TP_BG, outline='#cccccc', width=1)

# Directional arrow triangles (up / down / left / right)
ac    = '#cccccc'              # arrow colour (matches outline)
mid_x = tp_x + tp_w // 2      # touchpad horizontal centre
mid_y = tp_y + tp_h // 2      # touchpad vertical centre
a     = mm(0.535)              # arrow triangle half-size (0.63 * 0.85)

ad = mm(2.25)                  # arrow distance from trackpad edge (2.5 * 0.9)
draw.polygon([(mid_x, tp_y + ad),                              # up
              (mid_x - a, tp_y + ad + a),
              (mid_x + a, tp_y + ad + a)], fill=ac)
draw.polygon([(mid_x, tp_y + tp_h - ad),                       # down
              (mid_x - a, tp_y + tp_h - ad - a),
              (mid_x + a, tp_y + tp_h - ad - a)], fill=ac)
draw.polygon([(tp_x + ad, mid_y),                              # left
              (tp_x + ad + a, mid_y - a),
              (tp_x + ad + a, mid_y + a)], fill=ac)
draw.polygon([(tp_x + tp_w - ad, mid_y),                       # right
              (tp_x + tp_w - ad - a, mid_y - a),
              (tp_x + tp_w - ad - a, mid_y + a)], fill=ac)

# =====================================================================
# SIDE BUTTONS — 3 per side, flanking the touchpad
# =====================================================================
btn_w_big    = mm(9.8)         # side-button hole width
btn_h_big    = mm(6.1)         # side-button hole height (matches keypad num_h)
side_btn_gap = mm(4.10)        # vertical gap between side button holes

btn_inset = mm(0.20)           # button cap is 0.20 mm smaller than hole on each side

def draw_btn(x, y, w, h, text, color=BTN, textcolor=TXT,
             font=font_btn, r=mm(1.05)):
    """Draw a button hole, then the raised button cap inset by 0.5 mm."""
    # Hole (recess in the plastic)
    draw.rounded_rectangle([x, y, x + w, y + h],
                           radius=r, fill=BTN_HOLE)
    # Button cap (centred inside the hole, 0.5 mm inset on all sides)
    i = btn_inset
    draw.rounded_rectangle([x + i, y + i, x + w - i, y + h - i],
                           radius=max(1, r - i), fill=color, outline=BTN_BORDER, width=1)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]    # text width
    th = bbox[3] - bbox[1]    # text height
    ty = bbox[1]               # top bearing offset
    draw.text((x + (w - tw) // 2 - bbox[0], y + (h - th) // 2 - ty),
              text, fill=textcolor, font=font)

def draw_sec(x, y, w, text):
    """Draw a secondary (blue) label centred above the button at (x, y)."""
    if not text:
        return
    bbox = draw.textbbox((0, 0), text, font=font_sec)
    tw = bbox[2] - bbox[0]
    draw.text((x + (w - tw) // 2, y - mm(2.2)),
              text, fill=TXT_SEC, font=font_sec)

# Left side: esc / pad / tab — aligned with keypad left column
left_x   = inner_x + btn_margin
left_btns = [("esc", "undo"), ("pad", "save"), ("tab", "")]

# Bottom button hole bottom is 3.5 mm from glass bottom; stack upward
# Top button (i=0) top should align with trackpad hole top
bot_btn_top = glass_bottom - mm(3.5) - btn_h_big  # top y of bottom button

for i, (label, sec) in enumerate(left_btns):
    # i=0 is top, i=2 is bottom
    by = bot_btn_top - (2 - i) * (btn_h_big + side_btn_gap)
    draw_btn(left_x, by, btn_w_big, btn_h_big, label, font=font_btn_sm)
    draw_sec(left_x, by, btn_w_big, sec)

# Right side: on / doc / menu — right edge aligned with keypad right column
right_x   = inner_r - btn_margin - btn_w_big
right_btns = [("on", "off"), ("doc", "+page"), ("menu", "")]

for i, (label, sec) in enumerate(right_btns):
    by = bot_btn_top - (2 - i) * (btn_h_big + side_btn_gap)
    draw_btn(right_x, by, btn_w_big, btn_h_big, label, font=font_btn_sm)
    draw_sec(right_x, by, btn_w_big, sec)

# =====================================================================
# KEYPAD BUTTON DIMENSIONS (all from caliper measurements)
# =====================================================================
# Button-area edges (inset 2.5 mm from inner panel edges on both sides)
kp_l = inner_x + btn_margin   # left  edge for keypad buttons
kp_r = inner_r - btn_margin   # right edge for keypad buttons

# Individual button sizes
num_w     = mm(9.8)            # standard / number button width
num_h     = mm(6.1)            # standard / number button height
dual_pair = mm(14.14)          # dual-button pair total width
dual_div  = mm(1.0)            # divider width between dual halves
enter_w   = mm(14.14)          # enter button width (= dual pair)
alpha_sz  = mm(4.48)           # alpha letter button side (square)
small_w   = mm(6.38)           # small special button width (EE, ?!)
small_h   = mm(4.48)           # small special button height
space_bw  = mm(12.0)           # space bar width
space_bh  = mm(4.8)            # space bar height

# Gaps between buttons
hg     = mm(2.9)               # horizontal gap: number buttons
vg     = mm(3.3)               # vertical gap: number / dual rows
hga    = mm(3.0)               # horizontal gap: alpha buttons
vga    = mm(2.4)               # vertical gap: alpha rows
gap_dg = mm(2.8)               # gap: dual-button column ↔ number grid

# =====================================================================
# ROW 0 — CTRL ROW  (ctrl, shift, var, del)
# =====================================================================
# First button row starts 4 mm below keypad top.
r0_y = keypad_y + mm(4)

# Right dual/enter column: pinned to kp_r
rdual_x  = kp_r - dual_pair                   # right dual-button column left edge

# Number grid: centred between left duals right edge and right duals left edge
_num_grid_w   = 3 * num_w + 2 * hg            # total number grid width
_between_duals = rdual_x - (kp_l + dual_pair)  # space between dual columns
_num_gap      = (_between_duals - _num_grid_w) // 2  # symmetric gap each side
grid_l   = kp_l + dual_pair + _num_gap         # grid left edge
grid_col = [grid_l + i * (num_w + hg) for i in range(3)]  # column x's

draw_btn(kp_l, r0_y, num_w, num_h, "ctrl",
         color=BTN_CTRL, font=font_btn_sm)
draw_btn(grid_col[0], r0_y, num_w, num_h, "shift", font=font_btn_sm)
draw_sec(grid_col[0], r0_y, num_w, "CAPS")
draw_btn(grid_col[2], r0_y, num_w, num_h, "var", font=font_btn_sm)
draw_sec(grid_col[2], r0_y, num_w, "sto\u2192")
draw_btn(rdual_x + dual_pair - num_w, r0_y, num_w, num_h, "del", font=font_btn_sm)
draw_sec(rdual_x + dual_pair - num_w, r0_y, num_w, "clear")

# =====================================================================
# ROWS 1–4 — DUAL BUTTONS + NUMBER GRID
# =====================================================================
r1_y = r0_y + num_h + vg      # Y of the first dual / number row

def draw_dual(x, y, t1, t2, s1="", s2=""):
    """Draw a dual-button pair: one long hole, two button caps inside."""
    r = mm(1.05)
    i = btn_inset
    hw = (dual_pair - dual_div) // 2   # half-width of each button hole

    # One long hole spanning both buttons
    draw.rounded_rectangle([x, y, x + dual_pair, y + num_h],
                           radius=r, fill=BTN_HOLE)

    # Left button cap (inset from hole edges, right side flush with divider)
    draw.rounded_rectangle([x + i, y + i, x + hw - i, y + num_h - i],
                           radius=max(1, r - i), fill=BTN, outline=BTN_BORDER, width=1)
    bbox = draw.textbbox((0, 0), t1, font=font_btn_sm)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    ty = bbox[1]
    draw.text((x + (hw - tw) // 2 - bbox[0], y + (num_h - th) // 2 - ty),
              t1, fill=TXT, font=font_btn_sm)

    # Right button cap
    rx = x + hw + dual_div
    draw.rounded_rectangle([rx + i, y + i, rx + hw - i, y + num_h - i],
                           radius=max(1, r - i), fill=BTN, outline=BTN_BORDER, width=1)
    bbox = draw.textbbox((0, 0), t2, font=font_btn_sm)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    ty = bbox[1]
    draw.text((rx + (hw - tw) // 2 - bbox[0], y + (num_h - th) // 2 - ty),
              t2, fill=TXT, font=font_btn_sm)

    # Thin vertical divider between the two caps
    dx = x + hw
    draw.rectangle([dx, y + mm(0.3), dx + dual_div, y + num_h - mm(0.3)],
                   fill=DIVIDER)
    if s1:
        draw_sec(x, y, hw, s1)
    if s2:
        draw_sec(rx, y, hw, s2)

# Left dual-button column  (4 rows, left-aligned at kp_l)
#   (left_label, right_label, left_secondary, right_secondary)
left_duals = [
    ("=",       "trig",     "\u2260\u2265\u25b8", "?"),
    ("^",       "x\u00b2",  "\u207f\u221ax",      "\u221a"),
    ("e\u02e3", "10\u02e3", "ln",                  "log"),
    ("(",       ")",        "[ ]",                 "{ }"),
]
for i, (t1, t2, s1, s2) in enumerate(left_duals):
    ry = r1_y + i * (num_h + vg)
    draw_dual(kp_l, ry, t1, t2, s1, s2)

# Right dual-button column  (3 rows, positioned 2.8 mm after number grid)
right_duals = [
    ("tmpl",   "cat",     ":=",          "\u221e\u03b2\u00b0"),
    ("\u00d7", "\u00f7",  "\"\u25a1\"",  "frac"),
    ("+",      "\u2212",  "",            ""),
]
for i, (t1, t2, s1, s2) in enumerate(right_duals):
    ry = r1_y + i * (num_h + vg)
    draw_dual(rdual_x, ry, t1, t2, s1, s2)

# Enter button  (row 4, right side — same width as a dual pair)
enter_y = r1_y + 3 * (num_h + vg)
draw_btn(rdual_x, enter_y, enter_w, num_h, "enter", font=font_btn_sm)
draw_sec(rdual_x, enter_y, enter_w, "\u2248")

# Number grid: 4 rows x 3 columns  (7-8-9 / 4-5-6 / 1-2-3 / 0-.-(-))
nums     = [["7","8","9"], ["4","5","6"], ["1","2","3"], ["0",".","(-)"]]
num_secs = [["","",""],    ["","",""],    ["","",""],    ["","capture","ans"]]
for ri, row in enumerate(nums):
    for ci, label in enumerate(row):
        nx = grid_col[ci]
        ny = r1_y + ri * (num_h + vg)
        draw_btn(nx, ny, num_w, num_h, label, color=BTN_NUM, font=font_btn_sm)
        s = num_secs[ri][ci]
        if s:
            draw_sec(nx, ny, num_w, s)

# =====================================================================
# ALPHA ROWS — 4 rows of letter + special buttons
# =====================================================================
# Sit below the number grid with a 1.5 mm extra gap.
alpha_y0 = r1_y + 4 * (num_h + vg) + mm(1.5)

# Anchor positions derived from the dual-button and enter-button columns:
#   EE left  edge = kp_l          (aligns with ctrl / = / ( dual buttons)
#   ?! right edge = rdual_x + enter_w  (aligns with right-side dual / enter)
#   A  right edge = kp_l + dual_pair   (aligns with ")" right edge)
#   G  left  edge = rdual_x           (aligns with enter left edge)
#   B–F are evenly distributed between A and G.
ee_x  = kp_l                                # EE left edge
a_x   = kp_l + dual_pair - alpha_sz         # A left edge (right edge at kp_l + dual_pair)
g_x   = rdual_x                             # G left edge (aligns with enter/right duals)
qm_x  = rdual_x + enter_w - small_w         # ?! left edge (right edge at rdual_x + enter_w)

# B–F: 5 buttons between A's right edge and G's left edge, evenly spaced
bf_space = g_x - (a_x + alpha_sz)           # available space for B–F
bf_gap   = (bf_space - 5 * alpha_sz) / 6    # equal gaps (before B, B–C, …, after F)

alpha_col_x = [ee_x, a_x]                   # cols 0 (EE), 1 (A)
for k in range(5):                           # cols 2–6 (B–F)
    alpha_col_x.append(round(a_x + alpha_sz + bf_gap + k * (alpha_sz + bf_gap)))
alpha_col_x.append(g_x)                     # col 7 (G)
alpha_col_x.append(qm_x)                    # col 8 (?!)

# Row 0: EE A B C D E F G ?!
#   is_small=1 → small_w/h;  is_small=0 → alpha_sz (square)
alpha_row0 = [("EE",1),("A",0),("B",0),("C",0),("D",0),
              ("E",0),("F",0),("G",0),("?!",1)]
ay = alpha_y0
for j, (lbl, is_small) in enumerate(alpha_row0):
    bw = small_w if is_small else alpha_sz
    bh = small_h if is_small else alpha_sz
    draw_btn(alpha_col_x[j], ay, bw, bh, lbl, font=font_alpha, r=mm(0.6))

# Row 1: pi> H I J K L M N flag
alpha_row1 = [("\u03c0\u25b8",1),("H",0),("I",0),("J",0),("K",0),
              ("L",0),("M",0),("N",0),("\u2691",1)]
ay = alpha_y0 + 1 * (alpha_sz + vga)
for j, (lbl, is_small) in enumerate(alpha_row1):
    bw = small_w if is_small else alpha_sz
    bh = small_h if is_small else alpha_sz
    draw_btn(alpha_col_x[j], ay, bw, bh, lbl, font=font_alpha, r=mm(0.6))

# Row 2: , O P Q R S T U return
alpha_row2 = [(",",1),("O",0),("P",0),("Q",0),("R",0),
              ("S",0),("T",0),("U",0),("\u23ce",1)]
ay = alpha_y0 + 2 * (alpha_sz + vga)
for j, (lbl, is_small) in enumerate(alpha_row2):
    bw = small_w if is_small else alpha_sz
    bh = small_h if is_small else alpha_sz
    draw_btn(alpha_col_x[j], ay, bw, bh, lbl, font=font_alpha, r=mm(0.6))

# Row 3: V W X Y Z [space bar]
# Each letter sits at its corresponding column (cols 1–5) so they
# align with O P Q R S in row 2 above.
ay  = alpha_y0 + 3 * (alpha_sz + vga)
for k, lbl in enumerate(["V", "W", "X", "Y", "Z"]):
    draw_btn(alpha_col_x[1 + k], ay, alpha_sz, alpha_sz, lbl, font=font_alpha, r=mm(0.6))

# Space bar at column 6 position
space_x = alpha_col_x[6]
draw_btn(space_x, ay, space_bw, space_bh, "", font=font_alpha, r=mm(0.6))
# Underline indicator across the space bar
draw.line([space_x + mm(2), ay + space_bh - mm(1.2),
           space_x + space_bw - mm(2), ay + space_bh - mm(1.2)],
          fill=TXT_MUTED, width=1)

# =====================================================================
# LAYER 6 — ALPHA MASK (rounds the outer corners of the image)
# =====================================================================
# The physical calculator has square top corners and rounded bottom
# corners.  A greyscale mask matching this silhouette is applied as the
# alpha channel, making outside corners transparent.
#
# The mask uses the full image height H (= shell_bottom), not
# blue_bottom, so it encompasses the entire rendered area.
mask = Image.new('L', (W, H), 0)   # black = fully transparent
m    = ImageDraw.Draw(mask)

n_arc_mask = 100               # arc resolution for mask polygon
mask_r = shell_corner_r        # matches blue shell corner radius

mask_pts = []

# Top edge (square corners, starts at glass_y to leave 1 px padding)
mask_pts.append((0, glass_y))
mask_pts.append((W, glass_y))

# Right side down to where the bottom-right arc begins
mask_pts.append((W, H - mask_r))

# Bottom-right arc: theta 0 → pi/2
cx, cy = W - mask_r, H - mask_r
for i in range(n_arc_mask + 1):
    t = (math.pi / 2) * i / n_arc_mask
    mask_pts.append((cx + mask_r * math.cos(t), cy + mask_r * math.sin(t)))

# Flat bottom between arcs
mask_pts.append((mask_r, H))

# Bottom-left arc: theta pi/2 → pi
cx, cy = mask_r, H - mask_r
for i in range(n_arc_mask + 1):
    t = math.pi / 2 + (math.pi / 2) * i / n_arc_mask
    mask_pts.append((cx + mask_r * math.cos(t), cy + mask_r * math.sin(t)))

# Left side back up
mask_pts.append((0, H - mask_r))
mask_pts.append((0, glass_y))

m.polygon(mask_pts, fill=255)  # white = fully opaque inside silhouette
img.putalpha(mask)

# Composite onto white background so outside corners are white, not transparent
bg = Image.new('RGBA', (W, H), (255, 255, 255, 255))
bg.paste(img, (0, 0), img)
img = bg

# =====================================================================
# SAVE
# =====================================================================
out_dir  = os.path.dirname(os.path.abspath(__file__))
out_path = os.path.join(out_dir, 'ti_nspire_render.png')

# Embed DPI so the image displays at 1:1 physical scale.
# S px/mm * 25.4 mm/inch = DPI
render_dpi = S * 25.4
img.save(out_path, dpi=(render_dpi, render_dpi))
print(f"Saved {W}x{H} image to {out_path}  ({render_dpi:.1f} DPI = 1:1 scale)")

# =====================================================================
# ELEMENT SIZES
# =====================================================================
def px2mm(v):
    return v / S

def sz(label, w, h=None):
    """Format a size line: label, mm dimensions, px dimensions."""
    if h is not None:
        return f"  {label:<34} {px2mm(w):>6.2f} x {px2mm(h):<6.2f} mm    {w:>4} x {h:<4} px"
    else:
        return f"  {label:<34} {px2mm(w):>6.2f} mm             {w:>4} px"

def pos(label, x, y):
    """Format a position line."""
    return f"  {label:<34} x={x:<5}  y={y:<5}       ({px2mm(x):>.2f}, {px2mm(y):.2f}) mm"

# Computed values for display
_btn_cap_inset = btn_inset * 2
_num_cap_w = num_w - _btn_cap_inset
_num_cap_h = num_h - _btn_cap_inset
_side_cap_w = btn_w_big - _btn_cap_inset
_side_cap_h = btn_h_big - _btn_cap_inset
_alpha_cap = alpha_sz - _btn_cap_inset
_small_cap_w = small_w - _btn_cap_inset
_small_cap_h = small_h - _btn_cap_inset
_space_cap_w = space_bw - _btn_cap_inset
_space_cap_h = space_bh - _btn_cap_inset
_dual_hw = (dual_pair - dual_div) // 2
_dual_cap_w = _dual_hw - _btn_cap_inset
_dual_cap_h = num_h - _btn_cap_inset
_enter_cap_w = enter_w - _btn_cap_inset
_enter_cap_h = num_h - _btn_cap_inset
_num_to_rdual = rdual_x - (grid_col[2] + num_w)
_outline_outer_w = outline_total_w
_outline_outer_h = outline_total_h
_top_btn_y = bot_btn_top - 2 * (btn_h_big + side_btn_gap)

print(f"\n{'─'*72}")
print(f"  {'ELEMENT':<34} {'SIZE (mm)':>16}    {'SIZE (px)':>12}")
print(f"{'─'*72}")

print(f"\n  IMAGE / SHELL")
print(sz("Canvas",                        W, H))
print(sz("Scale factor",                  round(S * 100) ))  # px per 100mm
print(sz("Shell corner radius",           shell_corner_r))
print(sz("Mask corner radius",            shell_corner_r))
print(sz("Top padding",                   glass_y))

print(f"\n  BLUE SHELL")
print(sz("Blue shell (full width)",       W, round(blue_bottom - glass_y)))
print(sz("Blue border side width",        bezel_side))
print(sz("Blue shell height",             blue_h))
print(sz("Blue bottom to calc bottom",    shell_bottom - blue_bottom))
print(pos("Blue shell position",          0, glass_y))

print(f"\n  BODY")
print(sz("Body rectangle",               W, H - glass_y))
print(pos("Body position",               0, glass_y))

print(f"\n  GLASS PANEL")
print(sz("Glass panel",                   inner_w, glass_h))
print(pos("Glass position",              inner_x, glass_y))
print(sz("Glass bottom from calc top",   glass_h))

print(f"\n  KEYPAD PANEL")
print(sz("Keypad panel",                  inner_w, keypad_h))
print(sz("Keypad corner radius",          kp_corner_r))
print(pos("Keypad position",             inner_x, keypad_y))
print(sz("Keypad to shell bottom",       shell_bottom - keypad_y))

print(f"\n  SCREEN (LCD)")
print(sz("Screen",                        scr_w, scr_h))
print(pos("Screen position",             scr_x, scr_y))
print(sz("Screen top from glass top",    scr_y - glass_y))
print(sz("Screen side margin",           scr_x - inner_x))

print(f"\n  LOGO")
print(pos("Logo position",               scr_x + (scr_w - logo_tw) // 2, round(logo_y0)))
print(sz("Logo text width",              logo_tw))

print(f"\n  TRACKPAD")
print(sz("White outline (outer)",         round(_outline_outer_w), round(_outline_outer_h)))
print(sz("White outline thickness",       round(tp_outline_thick)))
print(sz("White outline gap to hole",     tp_outline_gap))
print(pos("White outline position",       outline_x, outline_y))
print(sz("Trackpad hole",                hole_w, hole_h))
print(sz("Trackpad hole margin",          tp_hole_margin))
print(pos("Trackpad hole position",       hole_x, hole_y))
print(sz("Trackpad surface",             tp_w, tp_h))
print(pos("Trackpad surface position",   tp_x, tp_y))
print(sz("Click button",                 cb, cb))
print(pos("Click button position",       cb_x, cb_y))
print(sz("Arrow triangle half-size",      a))
print(sz("Hole bottom to glass bottom",  (glass_y + glass_h) - (hole_y + hole_h)))

print(f"\n  SIDE BUTTONS")
print(sz("Side button hole",             btn_w_big, btn_h_big))
print(sz("Side button cap",              _side_cap_w, _side_cap_h))
print(sz("Side button v-gap",            side_btn_gap))
print(pos("Left buttons x",              left_x, round(_top_btn_y)))
print(pos("Right buttons x",             right_x, round(_top_btn_y)))
print(sz("Bottom btn to glass bottom",   (glass_y + glass_h) - (bot_btn_top + btn_h_big)))

print(f"\n  CTRL ROW")
print(sz("Ctrl/del button hole",         num_w, num_h))
print(sz("Ctrl/del button cap",          _num_cap_w, _num_cap_h))
print(pos("Ctrl row position",           kp_l, r0_y))

print(f"\n  DUAL BUTTONS")
print(sz("Dual pair hole",               dual_pair, num_h))
print(sz("Dual half cap",                _dual_cap_w, _dual_cap_h))
print(sz("Dual divider width",           dual_div))
print(pos("Left duals position",         kp_l, r1_y))
print(pos("Right duals position",        rdual_x, r1_y))

print(f"\n  NUMBER GRID")
print(sz("Number button hole",           num_w, num_h))
print(sz("Number button cap",            _num_cap_w, _num_cap_h))
print(pos("Grid top-left (7)",           grid_col[0], r1_y))
print(pos("Grid top-right (9)",          grid_col[2], r1_y))
print(f"  {'Grid columns x':<34} {grid_col[0]:>4}, {grid_col[1]:>4}, {grid_col[2]:>4} px")

print(f"\n  ENTER BUTTON")
print(sz("Enter hole",                   enter_w, num_h))
print(sz("Enter cap",                    _enter_cap_w, _enter_cap_h))
print(pos("Enter position",              rdual_x, enter_y))

print(f"\n  ALPHA BUTTONS")
print(sz("Alpha button hole",            alpha_sz, alpha_sz))
print(sz("Alpha button cap",             _alpha_cap, _alpha_cap))
print(sz("Small button hole (EE/?!)",    small_w, small_h))
print(sz("Small button cap",             _small_cap_w, _small_cap_h))
print(pos("Alpha row 0 position",        alpha_col_x[0], alpha_y0))
print(f"  {'Alpha columns x':<34} {', '.join(str(x) for x in alpha_col_x)} px")

print(f"\n  SPACE BAR")
print(sz("Space bar hole",               space_bw, space_bh))
print(sz("Space bar cap",                _space_cap_w, _space_cap_h))
print(pos("Space bar position",          space_x, round(alpha_y0 + 3 * (alpha_sz + vga))))

print(f"\n  GAPS & MARGINS")
print(sz("Button inset (all)",            btn_inset))
print(sz("Number button h-gap",          hg))
print(sz("Number button v-gap",          vg))
print(sz("Dual to number gap (L)",       grid_col[0] - (kp_l + dual_pair)))
print(sz("Number to right dual gap (R)", rdual_x - (grid_col[2] + num_w)))
print(sz("Alpha button h-gap",           hga))
print(sz("Alpha B-F computed gap",       round(bf_gap)))
print(sz("Alpha button v-gap",           vga))
print(sz("Keypad edge margin (L)",       kp_l - inner_x))
print(sz("Keypad edge margin (R)",       inner_r - (rdual_x + dual_pair)))
print(sz("Inner panel inset (bezel)",    bezel_side))

print(f"{'─'*72}")
