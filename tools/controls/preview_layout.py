"""Simulates DefaultLayoutProvider portrait layout on a Galaxy S25+ and renders it with the generated drawables."""
import sys
sys.argv = [sys.argv[0], sys.argv[1], sys.argv[2]]
exec(open(__file__.replace("preview_layout.py", "preview_controls.py"), encoding="utf-8").read().replace("\nmain()\n", "\n"))

folder = Path(sys.argv[1]); out = sys.argv[2]
dp = 2.8125
W, H = 1080, 2340
top_inset, bottom_inset = 94, 0
d = lambda v: int(v * dp)
ratio = 256 / 192
sw = W; sh = int(W / ratio)
screens_bottom = top_inset + sh * 2
utility_top = screens_bottom if screens_bottom + d(50) + d(140) <= H else top_inset + sh
center_pads = utility_top == screens_bottom

margin, gap, lr, small, minp, maxp = d(8), d(6), d(50), d(40), d(110), d(150)
left, right, top, bottom = margin, W - margin, utility_top + gap, H - bottom_inset - margin
cx = W // 2
comps = [("button_l", left, top, lr), ("button_r", right - lr, top, lr)]
utils = ["button_toggle_lid", "ic_touch_enabled", "button_microphone", "button_fast_forward", "button_pause"]
space = (right - left) - 2 * (lr + gap)
spacing = max(d(2), min(d(10), (space - len(utils) * small) // (len(utils) - 1)))
rw = len(utils) * small + (len(utils) - 1) * spacing
ux = cx - rw // 2
for u in utils:
    comps.append((u, ux, top + (lr - small) // 2, small)); ux += small + spacing
pads_area_top = top + lr + gap
wfp = ((right - left) - 2 * small - 3 * gap) // 2
hfp = bottom - pads_area_top
pad = max(minp, min(maxp, min(wfp, max(hfp, minp))))
pads_top = pads_area_top + (hfp - pad) // 2 if center_pads and hfp > pad else bottom - pad
comps += [("keypad", left, pads_top, pad), ("buttons", right - pad, pads_top, pad)]
st = pads_top + pad - small
comps += [("button_select", cx - gap // 2 - small, st, small), ("button_start", cx + gap // 2, st, small)]
print("pad dp:", pad / dp, "spacing dp:", spacing / dp, "free below screens dp:", (H - screens_bottom) / dp)

scale = 0.4
fig = plt.figure(figsize=(W * scale / 100, H * scale / 100), dpi=100)
ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")
fig.patch.set_facecolor("#000000")
ax.add_patch(plt.Rectangle((0, top_inset), sw, sh, color="#2f5d46"))
ax.add_patch(plt.Rectangle((0, top_inset + sh), sw, sh, color="#3d4f7a"))
for name, x, y, size in comps:
    draw(ax, folder / f"{name}.xml", x, y, size)
fig.savefig(out, facecolor=fig.get_facecolor())
