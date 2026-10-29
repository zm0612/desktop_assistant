from PIL import Image, ImageFont, ImageDraw

FONT_PATH = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
CELL, SIZE = 24, 22
f = ImageFont.truetype(FONT_PATH, SIZE)
for ch in "晴多云雨。，国":
    im = Image.new("L", (CELL, CELL), 0)
    bb = f.getbbox(ch)
    gw, gh = bb[2]-bb[0], bb[3]-bb[1]
    tmp = Image.new("L", (gw, gh), 0)
    ImageDraw.Draw(tmp).text((-bb[0], -bb[1]), ch, fill=255, font=f)
    im.paste(tmp, ((CELL-gw)//2, (CELL-gh)//2))
    im.resize((CELL*5, CELL*5)).save(f"t_{ch}.png")   # 放大5倍看是否完整
