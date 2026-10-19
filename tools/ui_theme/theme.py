#!/usr/bin/env python3
"""Hellish Sight UI theme: re-skins the game's Iggy skin libraries (every menu and container screen)
with the dark-glass + ember-orange look of the new main menu.

  theme.py <MediaWindows64 dir> <out dir> <ffdec.jar>

Shapes (panels, slots, tabs, tooltips) and text colours are recoloured through ffdec's XML export;
the button / input / slider / tickbox / progress bitmaps are redrawn and swapped in."""
import os, re, subprocess, sys, shutil
from PIL import Image, ImageDraw

MEDIA, OUT, FFDEC = sys.argv[1], sys.argv[2], sys.argv[3]
WORK = os.path.join(OUT, '_work')
os.makedirs(WORK, exist_ok=True)

def ffdec(*args):
    r = subprocess.run(['java', '-jar', FFDEC] + list(args), capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise SystemExit('ffdec failed: %s' % ' '.join(args))

# ---------------------------------------------------------------------------------------------
# palette
OUTLINE = (6, 7, 9)
FACE = (29, 32, 40)
FACE_DARK = (24, 27, 34)
HILITE = (58, 63, 77)
HILITE2 = (46, 50, 62)
SHADOW = (13, 14, 18)
SLOT = (17, 19, 24)
SLOT_SH = (8, 9, 12)
ORANGE = (255, 106, 30)
ORANGE_LT = (255, 154, 74)
ORANGE_DK = (190, 66, 14)
TEXT = (232, 234, 240)
TEXT_DIM = (168, 174, 188)

GREY_MAP = {
    255: HILITE, 235: HILITE2, 209: FACE, 198: FACE, 197: FACE, 194: FACE, 166: FACE_DARK,
    155: FACE_DARK, 154: FACE_DARK, 139: SLOT, 85: SHADOW, 84: SHADOW, 83: SHADOW, 55: SLOT_SH,
    49: SLOT_SH, 3: OUTLINE, 0: OUTLINE,
}

def map_grey(v):
    if v in GREY_MAP: return GREY_MAP[v]
    # smooth ramp for anything else: 0 -> outline, 255 -> highlight
    t = v / 255.0
    lo, hi = OUTLINE, HILITE
    return tuple(int(lo[i] + (hi[i] - lo[i]) * t) for i in range(3))

def map_shape_colour(r, g, b):
    if abs(r - g) <= 3 and abs(g - b) <= 3:
        return map_grey(round((r + g + b) / 3))
    if b > 200 and r < 80:					# selection blues -> ember orange
        return ORANGE if g < 170 else ORANGE_LT
    if r < 60 and g < 60 and b < 70:			# dark teal tooltip body
        return (16, 18, 23)
    return (r, g, b)

def map_text_colour(r, g, b):
    lum = 0.3 * r + 0.59 * g + 0.11 * b
    grey = abs(r - g) <= 12 and abs(g - b) <= 12
    if grey and lum < 80: return TEXT
    if grey and lum < 130: return TEXT_DIM
    return (r, g, b)

def recolour_xml(xml, shapes=True):
    def colour_tag(m):
        tag, attrs = m.group(1), m.group(2)
        r = int(re.search(r'red="(\d+)"', attrs).group(1)); g = int(re.search(r'green="(\d+)"', attrs).group(1)); b = int(re.search(r'blue="(\d+)"', attrs).group(1))
        if tag == 'textColor': nr, ng, nb = map_text_colour(r, g, b)
        elif tag == 'color' and shapes: nr, ng, nb = map_shape_colour(r, g, b)
        else: return m.group(0)
        attrs = re.sub(r'red="\d+"', 'red="%d"' % nr, attrs)
        attrs = re.sub(r'green="\d+"', 'green="%d"' % ng, attrs)
        attrs = re.sub(r'blue="\d+"', 'blue="%d"' % nb, attrs)
        return '<%s %s/>' % (tag, attrs)
    xml = re.sub(r'<(color|textColor) (type="RGBA?"[^>]*?)/>', colour_tag, xml)
    # colours inside the edit texts' initial HTML
    def html_colour(m):
        h = m.group(1)
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        nr, ng, nb = map_text_colour(r, g, b)
        return 'color=&quot;#%02x%02x%02x&quot;' % (nr, ng, nb)
    xml = re.sub(r'color=&quot;#([0-9A-Fa-f]{6})&quot;', html_colour, xml)
    return xml

# ---------------------------------------------------------------------------------------------
# bitmaps
def vgrad(draw, box, top, bottom):
    x0, y0, x1, y1 = box
    h = max(1, y1 - y0)
    for y in range(y0, y1):
        t = (y - y0) / (h - 1 if h > 1 else 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        draw.line([(x0, y), (x1 - 1, y)], fill=c + (255,))

def button(w, h, style, p=3):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if style == 'norm':
        border, top, bottom, hi, sh = OUTLINE, (40, 44, 54), (27, 30, 37), (66, 72, 88), (18, 20, 25)
    elif style == 'over':
        border, top, bottom, hi, sh = (58, 20, 5), (255, 128, 52), (224, 86, 27), (255, 184, 130), (176, 62, 14)
    else:  # disabled-looking flat
        border, top, bottom, hi, sh = OUTLINE, (30, 32, 38), (24, 26, 31), (44, 47, 56), (16, 17, 21)
    d.rectangle([0, 0, w - 1, h - 1], fill=border + (255,))
    vgrad(d, (p, p, w - p, h - p), top, bottom)
    d.rectangle([p, p, w - p - 1, p + p - 1], fill=hi + (255,))				# top light
    d.rectangle([p, h - 2 * p, w - p - 1, h - p - 1], fill=sh + (255,))		# bottom shade
    for (cx, cy) in ((0, 0), (w - p, 0), (0, h - p), (w - p, h - p)):			# pixel-rounded corners
        d.rectangle([cx, cy, cx + p - 1, cy + p - 1], fill=(0, 0, 0, 0))
    return im

def frame(w, h, colour, p=3, alpha=255):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for i in range(p):
        d.rectangle([i, i, w - 1 - i, h - 1 - i], outline=colour + (alpha,))
    for (cx, cy) in ((0, 0), (w - p, 0), (0, h - p), (w - p, h - p)):
        d.rectangle([cx, cy, cx + p - 1, cy + p - 1], fill=(0, 0, 0, 0))
    return im

def flat(w, h, fill, border=None, p=3, alpha=255):
    im = Image.new('RGBA', (w, h), fill + (alpha,))
    if border:
        d = ImageDraw.Draw(im)
        for i in range(p):
            d.rectangle([i, i, w - 1 - i, h - 1 - i], outline=border + (255,))
    return im

def bar(w, h, top, bottom):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    vgrad(ImageDraw.Draw(im), (0, 0, w, h), top, bottom)
    return im

def recolour_image(src):
    im = Image.open(src).convert('RGBA')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0: continue
            if abs(r - g) <= 14 and abs(g - b) <= 14:
                v = (r + g + b) / 3 / 255.0
                c = tuple(int(OUTLINE[i] + (HILITE[i] - OUTLINE[i]) * (v ** 1.25)) for i in range(3))
            elif b > r + 30:						# blue (hover) -> orange
                v = (r + g + b) / 3 / 255.0
                c = tuple(int(ORANGE_DK[i] + (ORANGE_LT[i] - ORANGE_DK[i]) * min(1.0, v * 1.4)) for i in range(3))
            elif r > 200 and g > 180 and b < 120:	# yellow -> light ember
                c = ORANGE_LT
            else:
                c = (r, g, b)
            px[x, y] = c + (a,)
    return im

def tick(src):
    im = Image.open(src).convert('RGBA')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a and r > 200 and g > 150 and b < 120: px[x, y] = ORANGE_LT + (a,)
    return im

def size_of(path):
    with Image.open(path) as im: return im.size

def graphics_bitmaps(names):
    """names: {characterId: exported png path}; returns {characterId: new png path}"""
    out = {}
    def put(cid, im):
        p = os.path.join(WORK, 'bmp_%d.png' % cid)
        im.save(p)
        out[cid] = p
    for cid, path in names.items():
        base = os.path.basename(path)
        w, h = size_of(path)
        n = base.split('_', 1)[1] if '_' in base else base
        if n in ('FJ_MainMenuButton_Norm.png', 'ListButton_Norm.png', 'Layout_Button_Bmp.png', 'LeaderboardButton_Norm.png'):
            put(cid, button(w, h, 'norm'))
        elif n in ('MainMenuButton_Over.png', 'ListButton_Over.png', 'LayoutButton_Over.png', 'LeaderboardButton_Over.png'):
            put(cid, button(w, h, 'over'))
        elif n in ('MainMenuButton_Outline.png', 'ListButton_Outline.png', 'LayoutButton_Outline.png'):
            put(cid, frame(w, h, (255, 200, 150)))
        elif n == 'TextInputBG.png':
            put(cid, flat(w, h, (14, 16, 21)))
        elif n == 'TextInputBGBorder.png':
            put(cid, frame(w, h, (58, 63, 77)))
        elif n == 'TextInputSelectedBorder.png':
            put(cid, frame(w, h, ORANGE))
        elif n == 'FJ_Slider_Background.png':
            put(cid, flat(w, h, (18, 20, 26), (46, 50, 62)))
        elif n == 'Slider_Graphic.png':
            put(cid, button(w, h, 'over'))
        elif n == 'SliderBackgroundHighlight.png':
            put(cid, flat(w, h, ORANGE, alpha=70))
        elif n == 'FJ_Tickbox_Background.png':
            put(cid, flat(w, h, (14, 16, 21), (52, 57, 70)))
        elif n == 'Tickbox_Over.png':
            put(cid, flat(w, h, (60, 32, 16), ORANGE))
        elif n in ('Tick.png', 'SmallTick.png', 'DLC_Tick.png', 'DLC_TickSmall.png'):
            put(cid, tick(path))
        elif n in ('ProgressbarBG.png', 'SaveBarBG.png'):
            put(cid, flat(w, h, (16, 18, 23), (40, 44, 54)))
        elif n in ('Progressbarfiller.png', 'SaveBar1.png'):
            put(cid, bar(w, h, ORANGE_LT, ORANGE_DK))
        elif n in ('Progressbarfiller2.png', 'SaveBar2.png'):
            put(cid, bar(w, h, (255, 200, 140), ORANGE))
        elif n in ('MoreOptionsTabGame.png', 'MoreOptionsTabWorld.png', 'MoreOptionsTabOff.png', 'IconHolder_1x1border.png',
                   'TexturePackSelectorBorder.png', 'CharacterBackground.png', 'CharacterBackground_Small.png',
                   'SkinSelectFrame.png', 'SkinSelect_TabBar.png', 'SkinSelect_TabBar_Selected.png', 'SkinSelect_TabNormal.png',
                   'SkinSelect_TabNormal_Selected.png', 'SkinSelect_TabOver.png', 'SkinSelect_TabOver_Selected.png',
                   'SkinSelect_TabBarSmall.png', 'SkinSelect_TabBarSmall_Selected.png', 'SkinSelect_TabNormalSmall.png',
                   'SkinSelect_TabNormalSmall_Selected.png', 'SkinSelect_TabOverSmall.png', 'SkinSelect_TabOverSmall_Selected.png',
                   'SkinSelect_TabBarSmallPanel.png', 'SkinSelect_TabBarSmallPanel_Selected.png', 'SkinSelect_BottomBar.png',
                   'SkinSelect_BottomBar_Selected.png', 'SkinSelectSmallHighlight.png', 'Craft_Scroll_up.png', 'Craft_Scroll_Down.png',
                   'scrollUp.png', 'scrollDown.png', 'scrollLeft.png', 'scrollRight.png'):
            put(cid, recolour_image(path))
    return out

# ---------------------------------------------------------------------------------------------
def process(name, bitmaps=False, shapes=True):
    src = os.path.join(MEDIA, name)
    if not os.path.exists(src):
        print('skip (missing)', name); return
    xml = os.path.join(WORK, name + '.xml')
    ffdec('-swf2xml', src, xml)
    s = open(xml, encoding='utf-8').read()
    s = recolour_xml(s, shapes)
    open(xml, 'w', encoding='utf-8').write(s)
    dst = os.path.join(OUT, name)
    ffdec('-xml2swf', xml, dst)
    if bitmaps:
        exp = os.path.join(WORK, name + '_img')
        shutil.rmtree(exp, ignore_errors=True)
        ffdec('-export', 'image', exp, src)
        names = {}
        for f in os.listdir(exp):
            m = re.match(r'(\d+)_', f)
            if m: names[int(m.group(1))] = os.path.join(exp, f)
        repl = graphics_bitmaps(names)
        if repl:
            args = []
            for cid, p in sorted(repl.items()): args += [str(cid), p]
            tmp = dst + '.tmp'
            ffdec('-replace', dst, tmp, *args)
            os.replace(tmp, dst)
        print(name, 'bitmaps replaced:', len(repl))
    print('themed', name)

process('skinHDGraphics.swf', bitmaps=True)
process('skinHDGraphicsInGame.swf', bitmaps=True)
process('skinHDGraphicsLabels.swf')
process('skinHD.swf')
process('skinHDInGame.swf')
shutil.rmtree(WORK, ignore_errors=True)
