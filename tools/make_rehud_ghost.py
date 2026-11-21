# Ghost-form HUD (shared by every HUD style except Original): cuts the user's ghost layout
# picture (transparent PNG, 1671x941) into pieces. Words are filled from their panel (the
# game draws its own), the timer's fill is cut out (the game's own green/red shows through),
# the spirit bar is marked as a meter (aux B) and the heart is cut out to beat.
import sys, json
import numpy as np, cv2
from PIL import Image, ImageDraw
src = np.array(Image.open(sys.argv[1]).convert('RGBA')).astype(np.float32); out = sys.argv[2]
def erase_text(img, box):
    x0, y0, x1, y1 = box
    sub = img[y0:y1, x0:x1]
    l = 0.30 * sub[..., 0] + 0.59 * sub[..., 1] + 0.11 * sub[..., 2]
    m = cv2.dilate((l > 150).astype(np.uint8), np.ones((9, 9), np.uint8))
    full = np.zeros(img.shape[:2], np.uint8); full[y0:y1, x0:x1] = m * 255
    res = cv2.inpaint(np.ascontiguousarray(img[..., :3]).astype(np.uint8), full, 7, cv2.INPAINT_NS)
    noise = np.random.default_rng(1).normal(0, 6, res.shape)
    img[..., :3] = np.where((full > 0)[..., None], np.clip(res + noise, 0, 255), img[..., :3])
pieces = {}
def piece(box):
    X0, Y0, X1, Y1 = box
    img = src[Y0:Y1, X0:X1].copy(); aux = np.zeros_like(img); aux[..., 3] = 255
    return img, aux
def save(name, box, img, aux, **extra):
    Image.fromarray(img.astype(np.uint8), 'RGBA').save(f'{out}/gh_{name}.png')
    Image.fromarray(aux.astype(np.uint8), 'RGBA').save(f'{out}/gh_{name}_aux.png')
    pieces[name] = dict(box=box, **extra)
def poly(box, pts):
    X0, Y0, X1, Y1 = box
    m = Image.new('L', (X1 - X0, Y1 - Y0), 0); ImageDraw.Draw(m).polygon([(x - X0, y - Y0) for x, y in pts], fill=255)
    return np.array(m) > 0
# top left: L / Float
box = (22, 50, 400, 318); img, aux = piece(box)
erase_text(img, (190 - 22, 100 - 50, 322 - 22, 150 - 50))
save('tl', box, img, aux, anchor=-1)
# top centre: timer frame, its fill cut out
box = (538, 56, 1163, 176); img, aux = piece(box)
inner = poly(box, [(570, 103), (1137, 103), (1137, 156), (570, 156)])
p = img; sat = (np.max(p[..., :3], -1) - np.min(p[..., :3], -1)) / np.maximum(1, np.max(p[..., :3], -1))
hole = inner & (sat > 0.35) & (np.max(p[..., :3], -1) > 60)
hole = cv2.dilate(hole.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool) & inner
img[hole, 3] = 0
ys, xs = np.nonzero(hole); print('tc hole', xs.min() + box[0], xs.max() + box[0], ys.min() + box[1], ys.max() + box[1])
save('tc', box, img, aux, anchor=0, screen=[int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1])
# bottom right: Possess / A
box = (1068, 726, 1643, 895); img, aux = piece(box)
erase_text(img, (1318 - 1068, 790 - 726, 1508 - 1068, 846 - 726))
save('br', box, img, aux, anchor=1)
# bottom left: heart, vines, spirit bar
box = (20, 566, 692, 894); img, aux = piece(box)
barp = poly(box, [(203, 776), (642, 776), (668, 846), (203, 846)])
p = img; sat = (np.max(p[..., :3], -1) - np.min(p[..., :3], -1)) / np.maximum(1, np.max(p[..., :3], -1))
meter = barp & (sat > 0.30) & (np.max(p[..., :3], -1) > 50)
aux[meter, 2] = 255
ys, xs = np.nonzero(meter); print('bar', xs.min() + box[0], xs.max() + box[0], ys.min() + box[1], ys.max() + box[1])
save('bl', box, img, aux, anchor=-1, bar=[int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1])
# the heart alone, for the beat (soft ellipse)
hb = (24, 584, 226, 850); X0, Y0 = hb[:2]
H = src[hb[1]:hb[3], hb[0]:hb[2]].copy()
m = Image.new('L', ((hb[2] - X0) * 4, (hb[3] - Y0) * 4), 0)
ImageDraw.Draw(m).ellipse([(30 - X0) * 4, (592 - Y0) * 4, (220 - X0) * 4, (842 - Y0) * 4], fill=255)
m = np.array(m.resize((hb[2] - X0, hb[3] - Y0), Image.LANCZOS)).astype(np.float32) / 255
m = cv2.GaussianBlur(m, (0, 0), 3)
H[..., 3] *= m
Image.fromarray(H.astype(np.uint8), 'RGBA').save(f'{out}/gh_heart.png')
pieces['heart'] = dict(box=list(hb))
json.dump(pieces, open(f'{out}/gh_layout.json', 'w'), indent=1); print(json.dumps(pieces))
# the heart's blue veins (aux R), which flash on each beat
h = np.array(Image.open(f'{out}/gh_heart.png')).astype(np.float32)
vein = (h[..., 2] > 110) & (h[..., 1] > 90) & (h[..., 2] > h[..., 0] * 1.35) & (h[..., 3] > 40)
ha = np.zeros_like(h); ha[..., 0] = np.clip((h[..., 2] - h[..., 0]) / 120.0, 0, 1) * vein * 255; ha[..., 3] = 255
Image.fromarray(ha.astype(np.uint8), 'RGBA').save(f'{out}/gh_heart_aux.png')
