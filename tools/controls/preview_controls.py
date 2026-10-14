"""Renders Android vector drawables (subset: M L H V Q C A Z, abs/rel) to a preview PNG with matplotlib."""
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import PathPatch
from matplotlib.path import Path as MPath

NS = "{http://schemas.android.com/apk/res/android}"
TOKEN = re.compile(r"[MmLlHhVvQqCcAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def arc_to_beziers(x0, y0, rx, ry, phi, large, sweep, x, y):
    # SVG arc endpoint -> center parameterization (phi assumed 0 here)
    if rx == 0 or ry == 0:
        return [((x, y),)]
    dx, dy = (x0 - x) / 2, (y0 - y) / 2
    x1p, y1p = dx, dy
    lam = (x1p ** 2) / rx ** 2 + (y1p ** 2) / ry ** 2
    if lam > 1:
        rx *= math.sqrt(lam); ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0, num / den)) if den else 0
    if large == sweep:
        coef = -coef
    cxp, cyp = coef * rx * y1p / ry, -coef * ry * x1p / rx
    cx, cy = cxp + (x0 + x) / 2, cyp + (y0 + y) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    segs = max(1, int(math.ceil(abs(dt) / (math.pi / 2))))
    out = []
    step = dt / segs
    for i in range(segs):
        a1, a2 = t1 + i * step, t1 + (i + 1) * step
        k = 4 / 3 * math.tan((a2 - a1) / 4)
        p1 = (cx + rx * (math.cos(a1) - k * math.sin(a1)), cy + ry * (math.sin(a1) + k * math.cos(a1)))
        p2 = (cx + rx * (math.cos(a2) + k * math.sin(a2)), cy + ry * (math.sin(a2) - k * math.cos(a2)))
        p3 = (cx + rx * math.cos(a2), cy + ry * math.sin(a2))
        out.append((p1, p2, p3))
    return out


def parse(d):
    toks = TOKEN.findall(d)
    verts, codes = [], []
    i, cmd = 0, None
    x = y = sx = sy = 0.0

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]; i += 1
            if cmd in "Zz":
                verts.append((sx, sy)); codes.append(MPath.CLOSEPOLY)
                x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            nx, ny = num(), num()
            if rel: nx += x; ny += y
            x, y, sx, sy = nx, ny, nx, ny
            verts.append((x, y)); codes.append(MPath.MOVETO)
            cmd = "l" if rel else "L"
        elif c == "L":
            nx, ny = num(), num()
            if rel: nx += x; ny += y
            x, y = nx, ny
            verts.append((x, y)); codes.append(MPath.LINETO)
        elif c == "H":
            nx = num(); x = nx + x if rel else nx
            verts.append((x, y)); codes.append(MPath.LINETO)
        elif c == "V":
            ny = num(); y = ny + y if rel else ny
            verts.append((x, y)); codes.append(MPath.LINETO)
        elif c == "Q":
            pts = [num(), num(), num(), num()]
            if rel: pts = [pts[0] + x, pts[1] + y, pts[2] + x, pts[3] + y]
            verts += [(pts[0], pts[1]), (pts[2], pts[3])]; codes += [MPath.CURVE3] * 2
            x, y = pts[2], pts[3]
        elif c == "C":
            pts = [num() for _ in range(6)]
            if rel: pts = [p + (x if k % 2 == 0 else y) for k, p in enumerate(pts)]
            verts += [(pts[0], pts[1]), (pts[2], pts[3]), (pts[4], pts[5])]; codes += [MPath.CURVE4] * 3
            x, y = pts[4], pts[5]
        elif c == "A":
            rx, ry, phi, large, sweep, nx, ny = num(), num(), num(), num(), num(), num(), num()
            if rel: nx += x; ny += y
            for seg in arc_to_beziers(x, y, rx, ry, phi, int(large), int(sweep), nx, ny):
                if len(seg) == 1:
                    verts.append(seg[0]); codes.append(MPath.LINETO)
                else:
                    verts += list(seg); codes += [MPath.CURVE4] * 3
            x, y = nx, ny
    return MPath(verts, codes)


def color(argb):
    a = int(argb[1:3], 16) / 255
    r, g, b = (int(argb[k:k + 2], 16) / 255 for k in (3, 5, 7))
    return (r, g, b, a)


def draw(ax, file, x, y, size):
    root = ET.parse(file).getroot()
    vp = float(root.get(NS + "viewportWidth"))
    s = size / vp
    px_per_unit = s  # figure units are pixels
    # Paths and groups (only translations), in drawing order
    items = []
    for child in root:
        if child.tag == "group":
            dx, dy = float(child.get(NS + "translateX") or 0), float(child.get(NS + "translateY") or 0)
            items += [(p, dx, dy) for p in child.findall("path")]
        elif child.tag == "path":
            items.append((child, 0.0, 0.0))
    for p, dx, dy in items:
        path = parse(p.get(NS + "pathData"))
        verts = [(x + (vx + dx) * s, y + (vy + dy) * s) for vx, vy in path.vertices]
        path = MPath(verts, path.codes)
        fill = p.get(NS + "fillColor")
        stroke = p.get(NS + "strokeColor")
        width = float(p.get(NS + "strokeWidth") or 0)
        ax.add_patch(PathPatch(path, facecolor=color(fill) if fill else "none",
                               edgecolor=color(stroke) if stroke else "none",
                               linewidth=width * px_per_unit * 72 / 100 if stroke else 0,
                               capstyle="round", joinstyle="round"))


def main():
    folder = Path(sys.argv[1]); out = sys.argv[2]
    W, H = 1080, 760
    fig = plt.figure(figsize=(W / 100, H / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")
    # Optional background colour, e.g. "#ffffff" to check the controls over a white screen
    fig.patch.set_facecolor(sys.argv[3] if len(sys.argv) > 3 else "#20242c")
    # Fake game screen strip on top
    ax.add_patch(plt.Rectangle((0, 0), W, 70, color="#3b6b4f"))
    dp = 2.8125
    draw(ax, folder / "button_l.xml", 34, 90, 50 * dp)
    draw(ax, folder / "button_r.xml", W - 34 - 50 * dp, 90, 50 * dp)
    utils = ["button_toggle_lid", "ic_touch_enabled", "button_microphone", "button_fast_forward", "button_pause"]
    total = len(utils) * 40 * dp + (len(utils) - 1) * 8 * dp
    ux = (W - total) / 2
    for u in utils:
        draw(ax, folder / f"{u}.xml", ux, 90 + 5 * dp, 40 * dp); ux += 48 * dp
    draw(ax, folder / "keypad.xml", 34, 300, 150 * dp)
    draw(ax, folder / "buttons.xml", W - 34 - 150 * dp, 300, 150 * dp)
    draw(ax, folder / "button_start.xml", W / 2 - 44 * dp, 300 + 110 * dp, 40 * dp)
    draw(ax, folder / "button_select.xml", W / 2 + 4 * dp, 300 + 110 * dp, 40 * dp)
    # Second row: remaining icons
    rest = ["button_fast_forward_disabled", "button_microphone_disabled", "ic_touch_disabled", "button_rewind",
            "button_reset", "button_swap_screens", "button_quick_save", "button_quick_load"]
    rx = 34
    for u in rest:
        draw(ax, folder / f"{u}.xml", rx, 10, 22 * dp); rx += 26 * dp
    fig.savefig(out, facecolor=fig.get_facecolor())


main()
