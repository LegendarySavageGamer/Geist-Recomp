# Host HUD styles (F1 > HUD style) beyond "Resident Evil inspired": cuts one of the user's
# layout pictures (transparent PNG) into the five pieces the renderer draws, using a spec
# below, and writes layout.ini beside them. Nothing is generated: every pixel is the user's,
# except where baked-in words, numbers and the portrait are removed (the game draws its own).
#   python make_hud_style.py <style> <layout.png> <out dir>
import sys
import numpy as np, cv2
from PIL import Image, ImageDraw
STYLES = {
    'fortnite': dict(   # Geist_Complete_Fortnite_Style_HUD_Transparent_1920x1080.png
        pieces={'tl': (30, 56, 531, 252, -1), 'tr': (1406, 65, 1892, 260, 1), 'tc': (665, 86, 1249, 186, 0),
                'br': (1351, 859, 1893, 1025, 1), 'bl': (186, 785, 764, 1034, -1)},
        words={'tl': [(234, 104, 392, 160)], 'tr': [(1518, 102, 1722, 161), (1541, 180, 1749, 233)],
               'br': [(1438, 937, 1712, 1003)]},
        numbers={'bl': [(500, 830, 604, 932)]},    # baked "1 / 120": the game's own numbers go here
        number_sample=(493, 500),
        meters={'tl': ('yellow', [(92, 180), (442, 180), (397, 240), (92, 240)]),
                'tc': ('green', [(686, 98), (1234, 98), (1234, 162), (686, 162)]),
                'bl': ('cyan', [(222, 938), (694, 938), (730, 1010), (222, 1010)])},
        ammo_bars={'bl': [(258, 838, 497, 877), (258, 883, 497, 922)]},   # drawn by the game's own ammo bars
        left_edge=(200, 796, 1026),   # bl: a clean left border here instead of the portrait cut
        left_fill=(200, 248, 796, 866, 252),   # bl: the portrait's gun over the panel -> panel colour (x0,x1,y0,y1,sample x)
        barpin=None,             # (the health meter's left end, middle) set from the meter
        ammo=(505, 882),         # bl: the game's ammo numbers go here (left end, middle)
        outline=True,
        clean_fill=True,
        boost_panel=[(66, 178), (450, 178), (390, 252), (66, 252)],
    ),
    'resident_evil': dict(   # the user's second Resident Evil layout (1671x941)
        pieces={'tl': (23, 32, 465, 227, -1), 'tr': (1202, 33, 1652, 227, 1), 'tc': (502, 48, 1110, 162, 0),
                'br': (1154, 741, 1644, 905, 1), 'bl': (150, 700, 688, 917, -1)},
        words={'tl': [(196, 82, 338, 136)], 'tr': [(1328, 78, 1502, 130), (1328, 152, 1514, 207)],
               'br': [(1268, 826, 1484, 882)]},
        numbers={'bl': [(436, 726, 520, 802)]},
        number_sample=(428, 434),
        meters={'tl': ('yellow', [(65, 152), (392, 152), (345, 212), (65, 212)]),
                'tc': ('green', [(530, 76), (1086, 76), (1086, 128), (530, 128)])},
        ammo_bars={'bl': [(234, 732, 414, 762), (234, 770, 430, 800)]},
        monitor={'bl': (194, 822, 656, 900)},     # the heart monitor: drawn live with the host's health
        status_words=True,                         # Fine / Caution / Danger on it
        gun_fill=[(150, 232, 690, 806)], metal='grey', foot_cut=914,
        top_bar=(204, 262, 708, 728, 268),   # bl: the frame's top bar carried left to the new edge (x0, x1, y0, y1, from x)
        inner_clean=(209, 236, 724, 800),   # bl: inside the new edge: plain panel
        panel_left=[(204, 806, 14), (158, 917, 14)], panel_top=708,
        barpin=None,
        ammo=(446, 766),
        outline=False,
        clean_fill=True,
        pulse=False,
        boost_panel=[(23, 147), (465, 147), (465, 227), (23, 227)],
    ),
    'fallout': dict(    # the user's Fallout-style layout (1672x941)
        pieces={'tl': (19, 34, 464, 227, -1), 'tr': (1206, 34, 1657, 227, 1), 'tc': (544, 51, 1135, 227, 0),
                'br': (1165, 748, 1648, 902, 1), 'bl': (158, 684, 707, 904, -1)},
        words={'tl': [(190, 92, 322, 136)], 'tr': [(1346, 92, 1504, 136), (1348, 158, 1508, 200)],
               'tc': [(724, 170, 960, 214)], 'br': [(1246, 828, 1466, 874)]},
        numbers={'bl': [(430, 732, 494, 800)]},
        number_sample=(420, 428),
        meters={'tl': ('yellow', [(70, 150), (410, 150), (380, 210), (70, 210)]),
                'bl': ('yellow', [(258, 826), (664, 826), (664, 870), (258, 870)])},
        ammo_bars={'bl': [(234, 734, 404, 760), (234, 768, 420, 796)]},
        monitor={'tc': (568, 86, 1112, 156)},
        gun_fill=[(148, 222, 684, 806), (148, 192, 806, 906)],   # bl: the portrait over the frame -> the frame's metal (the panel runs on behind him)
        panel_left=[(150, 800, 12), (164, 906, 14)], panel_top=700,   # bl: the panel's left end per band (up to row y): x, corner radius
        barpin=None,
        ammo=(430, 766),
        outline=False,
        clean_fill=True,
        pulse=True,
        pulse_color=(1.0, 0.78, 0.20),   # a yellow shimmer
        health_colors=True,              # HP: yellow-green, orange-yellow, red
        ammo_stripes=True,               # ammo bars as diagonal stripes
        boost_panel=[(19, 153), (464, 153), (464, 227), (19, 227)],
    ),
}
style, src_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
S = STYLES[style]
src = np.array(Image.open(src_path).convert('RGBA')).astype(np.float32)
def fill_masked(img, box, X0, Y0, sample_x):
    """Baked bars and numbers on a plain panel: only their pixels (and outlines) change, to
    the panel colour of their row, taken from a clean column (sample_x) between them."""
    x0, y0, x1, y1 = box[0] - X0, box[1] - Y0, box[2] - X0, box[3] - Y0
    sub = img[y0:y1, x0:x1]
    l = 0.30 * sub[..., 0] + 0.59 * sub[..., 1] + 0.11 * sub[..., 2]
    sat = (sub[..., :3].max(-1) - sub[..., :3].min(-1)) / np.maximum(1, sub[..., :3].max(-1))
    m = (l > 120) | (l < 22) | ((sub[..., 1] > sub[..., 2]) & (sat > 0.3))
    m = cv2.dilate(m.astype(np.uint8), np.ones((5, 5), np.uint8)).astype(bool)
    col = np.median(img[y0:y1, sample_x[0] - X0:sample_x[1] - X0, :3], axis=1)
    col = cv2.GaussianBlur(col[:, None, :].astype(np.float32), (1, 0), 2.0)[:, 0, :]
    fill = np.repeat(col[:, None, :], x1 - x0, axis=1) + np.random.default_rng(3).normal(0, 2.0, (y1 - y0, x1 - x0, 3))
    soft = cv2.GaussianBlur(m.astype(np.float32), (0, 0), 1.2)[..., None]
    sub[..., :3] = sub[..., :3] * (1 - soft) + np.clip(fill, 0, 255) * soft
def fill_clean(img, box, X0, Y0, pad=(4, 12), right_only=False):
    """Replaces a word (or baked numbers) with the plain panel: each row takes the panel's
    colour just left and just right of it and runs smoothly from one to the other."""
    x0, y0, x1, y1 = box[0] - X0, box[1] - Y0, box[2] - X0, box[3] - Y0
    a, b = pad
    L = np.median(img[y0:y1, max(0, x0 - b):max(1, x0 - a), :3], axis=1)
    R = np.median(img[y0:y1, x1 + a:x1 + b, :3], axis=1)
    if right_only: L = R
    t = np.linspace(0, 1, x1 - x0)[None, :, None]
    fill = L[:, None, :] * (1 - t) + R[:, None, :] * t
    fill = cv2.GaussianBlur(fill.astype(np.float32), (0, 0), 1.5)
    noise = np.random.default_rng(2).normal(0, 2.0, fill.shape)
    img[y0:y1, x0:x1, :3] = np.clip(fill + noise, 0, 255)
def erase(img, box, X0, Y0, outline=True, light=None):
    x0, y0, x1, y1 = box[0] - X0, box[1] - Y0, box[2] - X0, box[3] - Y0
    sub = img[y0:y1, x0:x1]
    l = 0.30 * sub[..., 0] + 0.59 * sub[..., 1] + 0.11 * sub[..., 2]
    sat = (sub[..., :3].max(-1) - sub[..., :3].min(-1)) / np.maximum(1, sub[..., :3].max(-1))
    # the word and its dark outline: bright, or very dark, or (for baked bars) green
    if light is not None:
        m = l > light
        m = cv2.dilate(m.astype(np.uint8), np.ones((5, 5), np.uint8))
    else:
        m = (l > 140) | ((l < 28) & outline) | ((sub[..., 1] > sub[..., 2] * 1.2) & (sat > 0.4))
        m = cv2.dilate(m.astype(np.uint8), np.ones((7, 7), np.uint8))
    full = np.zeros(img.shape[:2], np.uint8); full[y0:y1, x0:x1] = m * 255
    res = cv2.inpaint(np.ascontiguousarray(img[..., :3]).astype(np.uint8), full, 9, cv2.INPAINT_TELEA)
    noise = np.random.default_rng(1).normal(0, 3, res.shape)
    img[..., :3] = np.where((full > 0)[..., None], np.clip(res + noise, 0, 255), img[..., :3])
lines = [f'# {style}: made by tools/make_hud_style.py from the user\'s layout picture', f'image={src.shape[1]},{src.shape[0]}']
for name, (x0, y0, x1, y1, anchor) in S['pieces'].items():
    img = src[y0:y1, x0:x1].copy(); aux = np.zeros_like(img); aux[..., 3] = 255
    if name == 'bl' and 'gun_fill' in S:
        # The panel's shape on the left, where the portrait covers it: flat ends with rounded
        # corners, one per band (the ammo box above, the HP bar below).
        H, W = img.shape[:2]
        shape = np.zeros((H, W), np.uint8)
        top = S.get('panel_top', y0) - y0
        for (lx, yb, rad) in S['panel_left']:
            yb = min(H, yb - y0); lx -= x0
            cv2.rectangle(shape, (lx + rad, top), (W - 1, yb - 1), 255, -1)
            cv2.rectangle(shape, (lx, top + rad), (W - 1, yb - 1 - rad), 255, -1)
            for cy in (top + rad, yb - 1 - rad): cv2.circle(shape, (lx + rad, cy), rad, 255, -1)
            top = yb
        inside = shape > 0
        reg = np.zeros((H, W), bool)
        for gx0, gx1, gy0, gy1 in S['gun_fill']: reg[max(0, gy0 - y0):gy1 - y0, max(0, gx0 - x0):gx1 - x0] = True
        gx1 = max(g[1] for g in S['gun_fill'])
        r_, g_, b_ = img[..., 0], img[..., 1], img[..., 2]
        mx = img[..., :3].max(-1); sat = (mx - img[..., :3].min(-1)) / np.maximum(1, mx)
        if S.get('metal') == 'grey':   # grey metal and dark purple panel
            metal = ((sat < 0.25) & (mx < 185)) | (mx < 60)
        else:
            metal = ((r_ >= g_ - 8) & (g_ >= b_ - 6) & (mx < 185) & (sat > 0.22) & ((r_ - b_) > 18)) | (mx < 40)
        fix = reg & inside & ~(metal & (img[..., 3] > 200))
        outl = np.zeros((H, W), bool); outl[:, :gx1 - x0] = True; outl &= ~inside
        m = cv2.dilate(fix.astype(np.uint8) * 255, np.ones((7, 7), np.uint8)) | (outl.astype(np.uint8) * 255)
        img[..., :3] = cv2.inpaint(np.ascontiguousarray(img[..., :3]).astype(np.uint8), m, 7, cv2.INPAINT_TELEA)
        img[(m > 0) & inside, 3] = 255
        # outside the shape on the left: gone (the portrait); a dark rim on the new edge
        left = np.zeros((H, W), bool); left[:, :gx1 - x0] = True
        img[left & ~inside, 3] = 0
        rim = (cv2.dilate(shape, np.ones((5, 5), np.uint8)) > 0) & ~(cv2.erode(shape, np.ones((7, 7), np.uint8)) > 0) & left & inside
        if S.get('metal') == 'grey':
            # the frame's own metal down the new edge: a bright band with a dark outline
            outer = (cv2.dilate(shape, np.ones((3, 3), np.uint8)) > 0) & ~(cv2.erode(shape, np.ones((3, 3), np.uint8)) > 0)
            rim = (cv2.dilate(shape, np.ones((3, 3), np.uint8)) > 0) & ~(cv2.erode(shape, np.ones((13, 13), np.uint8)) > 0) & left & inside
            band = rim & ~outer
            inner = rim & (cv2.erode(shape, np.ones((9, 9), np.uint8)) > 0)
            img[band, :3] = (172, 170, 166); img[inner, :3] = (60, 58, 60); img[outer & inside, :3] = (30, 28, 30)
            img[..., :3] = np.where((rim & left)[..., None], cv2.GaussianBlur(img[..., :3].astype(np.float32), (0, 0), 0.7), img[..., :3])
        else:
            img[rim, :3] *= 0.45
        img[S.get('foot_cut', H) - y0 if 'foot_cut' in S else H:, :, 3] = 0
        if 'inner_clean' in S:
            cx0, cx1, cy0, cy1 = S['inner_clean']
            dark = np.median(img[cy0 - y0 + 34:cy0 - y0 + 40, cx1 - x0 + 10:cx1 - x0 + 60, :3].reshape(-1, 3), axis=0)
            img[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0, :3] = dark
        if 'top_bar' in S:
            tx0, tx1, ty0, ty1, sx = S['top_bar']
            col = img[ty0 - y0:ty1 - y0, sx - x0].copy()
            for xx in range(tx0 - x0, tx1 - x0):
                keep = img[ty0 - y0:ty1 - y0, xx, 3] > 200
                # only where the source column is frame (not the dark inside below it)
                img[ty0 - y0:ty1 - y0, xx] = col
            # round the outer top-left corner of the carried bar
            rr = 10
            for yy in range(rr):
                for xx in range(rr):
                    if (rr - xx) ** 2 + (rr - yy) ** 2 > rr * rr:
                        img[ty0 - y0 + yy, tx0 - x0 + xx, 3] = 0
    if name == 'bl' and 'left_edge' in S:
        ex, ey0, ey1 = S['left_edge']; ex -= x0
        fx0, fx1, fy0, fy1, sx = S['left_fill']
        # the gun over the panel: the panel's colour of each row
        for yy in range(fy0 - y0, fy1 - y0):
            col = np.median(img[yy, sx - x0:sx - x0 + 4, :3], axis=0)
            row = img[yy, fx0 - x0:fx1 - x0]
            navy = (row[:, 2] > row[:, 0] + 40) & (row[:, :3].max(-1) < 140)
            row[~navy, :3] = col; row[~navy, 3] = 255
        img[:, :ex, 3] = 0
        # the border, as the frame's own: dark, bright cyan, dark, then the panel
        prof = [(1, 1, 39), (0, 33, 69), (3, 171, 216), (1, 230, 252), (3, 212, 252), (2, 175, 245), (0, 14, 72)]
        for yy in range(ey0 - y0, min(img.shape[0], ey1 - y0)):
            if img[yy, ex + 8, 3] < 128: continue
            for k, c in enumerate(prof):
                img[yy, ex + k, :3] = c; img[yy, ex + k, 3] = 255
        img[:, ex:ex + 7, :3] = cv2.GaussianBlur(img[:, ex:ex + 7, :3], (0, 0), (0.6))
        # specks of the portrait left in the tab above the panel, and below its foot
        top = img[:ey0 - y0 + 6, :ex + 50]
        top[~(top[..., 2] > top[..., 0] + 40), 3] = 0
        img[ey1 - y0:, :ex + 10, 3] = 0
        # inside the tab's curve, between the new edge and the curve: the panel
        navy = np.median(img[60:70, ex + 52:ex + 56, :3].reshape(-1, 3), axis=0)
        for yy in range(0, 75):
            xx = ex + 7
            while xx < ex + 90 and img[yy, xx, 3] < 128: xx += 1
            if xx < ex + 90 and yy >= ey0 - y0: img[yy, ex + 7:xx, :3] = navy; img[yy, ex + 7:xx, 3] = 255
    if name == 'bl' and 'portrait_x' in S:
        px = S['portrait_x'] - x0
        part = img[:, :px]
        blue = ((part[..., 2] > part[..., 0] + 45) & (part[..., 2] > part[..., 1] + 5)) | (part[..., :3].max(-1) < 30)
        blue = cv2.morphologyEx(blue.astype(np.uint8), cv2.MORPH_OPEN, np.ones((3, 3), np.uint8)) > 0
        part[..., 3] *= cv2.GaussianBlur(blue.astype(np.float32), (0, 0), 1.0)
        # the bar's left end, where a leg covered it: the bar beside it, carried over
        mk, poly = S['meters']['bl']
        m = Image.new('L', (x1 - x0, y1 - y0), 0); ImageDraw.Draw(m).polygon([(x - x0, y - y0) for x, y in poly], fill=255)
        inbar = np.array(m) > 0
        for yy, xx in zip(*np.nonzero(inbar[:, :px + 4])):
            img[yy, xx] = img[yy, min(img.shape[1] - 1, xx + 40)]
        # a soft left edge where the portrait was
        ramp = np.clip((np.arange(img.shape[1]) - 2) / 36.0, 0, 1)[None, :]
        img[..., 3] *= np.where(inbar, 1.0, ramp)
    for w in S['words'].get(name, []):
        if name == 'bl' and 'ammo_sample' in S: fill_masked(img, w, x0, y0, S['ammo_sample'])
        elif S.get('clean_fill'): fill_clean(img, w, x0, y0)
        else: erase(img, w, x0, y0, S.get('outline', True), S.get('word_light'))
    for w in S.get('numbers', {}).get(name, []): fill_masked(img, w, x0, y0, S['number_sample'])
    for k, (ax0, ay0, ax1, ay1) in enumerate(S.get('ammo_bars', {}).get(name, [])):
        sub = img[ay0 - y0:ay1 - y0, ax0 - x0:ax1 - x0]
        gm = (sub[..., 1] > 120) & (sub[..., 1] > sub[..., 2] * 1.3) & (sub[..., 1] > sub[..., 0] * 1.05)
        aux[ay0 - y0:ay1 - y0, ax0 - x0:ax1 - x0, 1][gm] = (k + 1) * 16
        # the bar's frame too (code + 8): with no ammo on screen the whole bar is plain panel
        fr = aux[ay0 - y0 - 3:ay1 - y0 + 3, ax0 - x0 - 3:ax1 - x0 + 3, 1]
        fr[fr == 0] = (k + 1) * 16 + 8
        ys, xs = np.nonzero(gm)
        lines.append(f'ammobar={k + 1},{xs.min() + ax0 - x0},{xs.max() + 1 + ax0 - x0}')
    if S.get('ammo_bars', {}).get(name) and 'number_sample' in S:
        bars = S['ammo_bars'][name]; sx0, sx1 = S['number_sample']
        col = np.median(img[bars[0][1] - y0:bars[-1][3] - y0, sx0 - x0:sx1 - x0, :3].reshape(-1, 3), axis=0) / 255.0
        lines.append('panel_color=' + ','.join(f'{v:.3f}' for v in col))
    if name in ('tl', 'tr', 'br') and S['words'].get(name):
        ws = S['words'][name]
        cx = (min(w[0] for w in ws) + max(w[2] for w in ws)) / 2 - x0; cy = (min(w[1] for w in ws) + max(w[3] for w in ws)) / 2 - y0
        lines.append(f'text={name},{cx:.1f},{cy:.1f}')
    if name in S.get('monitor', {}):
        mx0, my0, mx1, my1 = S['monitor'][name]
        img[my0 - y0:my1 - y0, mx0 - x0:mx1 - x0, 3] = 0
        lines.append(f'monitor={name},{mx0 - x0},{my0 - y0},{mx1 - x0},{my1 - y0}')
    lines.append(f'piece={name},{x0},{y0},{x1},{y1},{anchor}')
    if name in S['meters']:
        kind, poly = S['meters'][name]
        m = Image.new('L', (x1 - x0, y1 - y0), 0); ImageDraw.Draw(m).polygon([(x - x0, y - y0) for x, y in poly], fill=255)
        inside = np.array(m) > 0
        r, g, b = img[..., 0], img[..., 1], img[..., 2]
        test = {'yellow': (r > 150) & (g > 100) & (b < 110),
                'green': (g > 120) & (g > r * 1.3) & (g > b * 1.3),
                'cyan': (b > 140) & (g > 120) & (r < 140)}[kind]
        meter = inside & test & (img[..., 3] > 0)
        aux[meter, 2] = 255
        ys, xs = np.nonzero(meter)
        lines.append(f'meter={name},{xs.min()},{xs.max() + 1}')
        print(name, 'meter', meter.sum(), 'u', xs.min(), xs.max())
    if name == 'tl' and 'boost_panel' in S:
        m = Image.new('L', (x1 - x0, y1 - y0), 0); ImageDraw.Draw(m).polygon([(x - x0, y - y0) for x, y in S['boost_panel']], fill=255)
        aux[np.array(m) > 0, 3] = 128
    Image.fromarray(img.astype(np.uint8), 'RGBA').save(f'{out}/{name}.png')
    Image.fromarray(aux.astype(np.uint8), 'RGBA').save(f'{out}/{name}_aux.png')
lines.append(f'pulse={1 if S.get("pulse", True) else 0}')
for k in ('health_colors', 'ammo_stripes', 'status_words'):
    if S.get(k): lines.append(f'{k}=1')
if 'pulse_color' in S: lines.append('pulse_color=' + ','.join(str(v) for v in S['pulse_color']))
bx0, by0 = S['pieces']['bl'][:2]
if S['barpin'] is None:
    if 'bl' in S['meters']:
        ml = [l for l in lines if l.startswith('meter=bl,')][0].split(',')
        poly = S['meters']['bl'][1]
        S['barpin'] = (int(ml[1]) + bx0, (min(y for x, y in poly) + max(y for x, y in poly)) / 2)
    else:   # the health monitor stands for the bar: its left end and middle, and its width
        mx0, my0, mx1, my1 = S['monitor']['bl']
        S['barpin'] = (mx0, (my0 + my1) / 2)
        lines.append(f'meter=bl,{mx0 - bx0},{mx1 - bx0}')
lines.append(f'barpin={S["barpin"][0] - bx0},{S["barpin"][1] - by0}')
lines.append(f'ammo={S["ammo"][0] - bx0},{S["ammo"][1] - by0}')
open(f'{out}/layout.ini', 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
