from PIL import Image, ImageDraw, ImageFont
import os

ASSETS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "app", "src", "main", "assets")
os.makedirs(ASSETS_DIR, exist_ok=True)

def save(img, name):
    img.save(os.path.join(ASSETS_DIR, name))
    print(f"Created: {name}")

# bg_sky.png — 512x256, bright cyan blue sky
img = Image.new("RGB", (512, 256))
draw = ImageDraw.Draw(img)
for y in range(256):
    # Bright cyan at top, lighter cyan at bottom
    c = int(180 + (75 * y / 256))
    draw.line([(0, y), (512, y)], fill=(100, c, 255))
save(img, "bg_sky.png")

# clouds_far.png — 512x128, white clouds on transparent
img = Image.new("RGBA", (512, 128), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)
for cx, cy, r in [(80,60,40),(200,50,30),(350,65,45),(460,55,25)]:
    for dx in range(-r, r+1):
        for dy in range(-r//2, r//2+1):
            if dx*dx/(r*r) + dy*dy/((r//2)**2) <= 1:
                draw.point((cx+dx, cy+dy), fill=(255,255,255,200))
save(img, "clouds_far.png")

# clouds_near.png — 512x128, larger clouds
img = Image.new("RGBA", (512, 128), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)
for cx, cy, r in [(60,70,55),(250,60,40),(430,75,50)]:
    for dx in range(-r, r+1):
        for dy in range(-r//2, r//2+1):
            if dx*dx/(r*r) + dy*dy/((r//2)**2) <= 1:
                draw.point((cx+dx, cy+dy), fill=(255,255,255,220))
save(img, "clouds_near.png")

# plane_p1.png and plane_p2.png — user-provided custom sprites, do NOT regenerate

# ground_grass.png — 512x64, green grass strip (tiled horizontally)
img = Image.new("RGBA", (512, 64), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Solid grass base
draw.rectangle([0, 0, 512, 64], fill=(34, 139, 34))
# Grass texture with slightly darker horizontal stripes
for y in range(0, 64, 4):
    draw.line([(0, y), (512, y)], fill=(20, 120, 20), width=1)
# Random grass tufts
import random
random.seed(42)
for _ in range(80):
    x = random.randint(0, 512)
    y = random.randint(0, 32)
    h = random.randint(4, 12)
    draw.line([(x, y), (x, y-h)], fill=(50, 160, 50), width=1)
save(img, "ground_grass.png")

# ground_dirt.png — 512x32, brown dirt strip below grass
img = Image.new("RGBA", (512, 32), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Solid dirt base
draw.rectangle([0, 0, 512, 32], fill=(139, 90, 43))
# Dirt texture
for y in range(0, 32, 2):
    draw.line([(0, y), (512, y)], fill=(120, 70, 30), width=1)
save(img, "ground_dirt.png")

# trees_silhouette.png — 512x192, dark tree/forest silhouette layer
img = Image.new("RGBA", (512, 192), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Create tree silhouettes (dark green/brown)
import random
random.seed(123)
for i in range(0, 512, 80):
    x = i + random.randint(-20, 20)
    h = random.randint(80, 140)
    # Tree trunk
    draw.rectangle([x-3, 192-h, x+3, 192], fill=(60, 40, 20))
    # Tree foliage (multiple circles for bushy shape)
    for j in range(3):
        fy = 192 - h + j*20
        draw.ellipse([x-30, fy-25, x+30, fy+25], fill=(40, 60, 30))
    draw.ellipse([x-35, 192-h-30, x+35, 192-h+10], fill=(40, 60, 30))
save(img, "trees_silhouette.png")

# house.png — 128x128, red barn matching reference (red walls, white/gray roof, windows)
img = Image.new("RGBA", (128, 128), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Main barn walls (dark red/maroon brick)
draw.rectangle([16, 52, 112, 118], fill=(140, 45, 45))
# Brick texture lines
for by in range(56, 118, 6):
    draw.line([(16, by), (112, by)], fill=(120, 35, 35), width=1)
for bx in range(20, 112, 12):
    offset = 6 if ((bx-20)//12) % 2 == 0 else 0
    for by in range(52, 118, 12):
        draw.line([(bx+offset, by), (bx+offset, by+6)], fill=(120, 35, 35), width=1)
# Roof — white/light gray A-frame gable
draw.polygon([(10, 52), (118, 52), (64, 14)], fill=(220, 215, 210))
# Roof outline
draw.line([(10, 52), (64, 14)], fill=(180, 175, 170), width=2)
draw.line([(118, 52), (64, 14)], fill=(180, 175, 170), width=2)
draw.line([(10, 52), (118, 52)], fill=(160, 155, 150), width=1)
# Roof ridge shading
draw.line([(12, 51), (64, 16)], fill=(200, 195, 190), width=1)
# Windows — 4 small windows with dark panes
for wx in [28, 48, 72, 92]:
    draw.rectangle([wx-6, 72, wx+6, 90], fill=(60, 80, 110))  # dark glass
    draw.rectangle([wx-7, 71, wx+7, 91], outline=(100, 70, 50), width=1)  # frame
    draw.line([(wx, 71), (wx, 91)], fill=(100, 70, 50), width=1)  # cross pane
    draw.line([(wx-6, 81), (wx+6, 81)], fill=(100, 70, 50), width=1)
# Main door — center, double door
draw.rectangle([52, 88, 76, 118], fill=(110, 60, 30))
draw.line([(64, 88), (64, 118)], fill=(80, 40, 20), width=2)  # door split
draw.rectangle([52, 88, 76, 118], outline=(80, 40, 20), width=1)
# Door handles
draw.ellipse([59, 102, 62, 105], fill=(200, 180, 50))
draw.ellipse([66, 102, 69, 105], fill=(200, 180, 50))
# Foundation line
draw.rectangle([14, 116, 114, 120], fill=(100, 90, 80))
save(img, "house.png")

# bullet.png — 24x24, orange glowing cannonball
img = Image.new("RGBA", (24, 24), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Outer glow (red-orange)
draw.ellipse([1, 1, 23, 23], fill=(255, 80, 0, 120))
# Core (bright orange-yellow)
draw.ellipse([4, 4, 20, 20], fill=(255, 140, 20))
# Hot center highlight
draw.ellipse([7, 7, 17, 17], fill=(255, 200, 60))
# Bright spot
draw.ellipse([9, 8, 14, 13], fill=(255, 240, 150))
save(img, "bullet.png")

# smoke.png — 32x32, gray smoke puff (for damaged plane state 1)
img = Image.new("RGBA", (32, 32), (0,0,0,0))
draw = ImageDraw.Draw(img)
draw.ellipse([4, 4, 28, 28], fill=(120, 120, 120, 160))
draw.ellipse([8, 2, 24, 18], fill=(150, 150, 150, 140))
draw.ellipse([2, 8, 18, 24], fill=(100, 100, 100, 120))
save(img, "smoke.png")

# spark.png — 32x32, orange-yellow spark (for damaged plane state 2)
img = Image.new("RGBA", (32, 32), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Orange glow
draw.ellipse([6, 6, 26, 26], fill=(255, 120, 0, 180))
# Yellow core
draw.ellipse([10, 10, 22, 22], fill=(255, 220, 50, 220))
# Bright center
draw.ellipse([13, 13, 19, 19], fill=(255, 255, 200, 255))
save(img, "spark.png")

# explosion_0..4.png — 64x64 each, larger dramatic explosion frames (yellow-orange fireball)
import random
for frame in range(5):
    img = Image.new("RGBA", (64, 64), (0,0,0,0))
    draw = ImageDraw.Draw(img)
    random.seed(100 + frame)

    # Frame 0: Small core
    if frame == 0:
        size = 16
        draw.ellipse([24, 24, 40, 40], fill=(255, 200, 0))
    # Frame 1: Growing, more yellow
    elif frame == 1:
        draw.ellipse([16, 16, 48, 48], fill=(255, 200, 0))
        draw.ellipse([12, 12, 52, 52], fill=(255, 150, 0, 180))
    # Frame 2: Large peak explosion (orange-red)
    elif frame == 2:
        draw.ellipse([8, 8, 56, 56], fill=(255, 150, 0))
        draw.ellipse([4, 4, 60, 60], fill=(255, 100, 0, 150))
        draw.ellipse([2, 2, 62, 62], fill=(255, 50, 0, 100))
    # Frame 3: Dispersing with smoke
    elif frame == 3:
        draw.ellipse([10, 10, 54, 54], fill=(200, 100, 0, 200))
        draw.ellipse([6, 6, 58, 58], fill=(150, 80, 0, 120))
        draw.ellipse([2, 2, 62, 62], fill=(100, 60, 0, 80))
    # Frame 4: Fading smoke
    else:
        draw.ellipse([12, 12, 52, 52], fill=(100, 80, 60, 150))
        draw.ellipse([8, 8, 56, 56], fill=(80, 70, 60, 100))
        draw.ellipse([4, 4, 60, 60], fill=(70, 60, 50, 60))

    save(img, f"explosion_{frame}.png")

# blimp.png — 256x128, green blimp
img = Image.new("RGBA", (256, 128), (0,0,0,0))
draw = ImageDraw.Draw(img)
# Body
draw.ellipse([10, 20, 220, 90], fill=(60,180,60), outline=(30,120,30,255))
# Gondola
draw.rectangle([80, 88, 160, 108], fill=(200,150,50))
# White field for score
draw.rectangle([60, 32, 180, 78], fill=(240,240,240), outline=(100,100,100))
save(img, "blimp.png")

# digit sprites 0-9 — 16x24 each, red digits on transparent background
digit_chars = "0123456789"
for i, ch in enumerate(digit_chars):
    img = Image.new("RGBA", (16, 24), (0,0,0,0))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("arial.ttf", 18)
    except:
        font = ImageFont.load_default()
    # Draw digit centered, red color (matches reference blimp score)
    bbox = draw.textbbox((0,0), ch, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    draw.text(((16-tw)//2, (24-th)//2 - 2), ch, fill=(200, 40, 40), font=font)
    save(img, f"digit_{i}.png")

# btn_up.png — 128x128, up arrow
img = Image.new("RGBA", (128, 128), (0,0,0,80))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,124,124], radius=16, outline=(255,255,255,200), width=3)
# Up arrow
draw.polygon([(64,20),(100,70),(28,70)], fill=(255,255,255,220))
draw.rectangle([50,68,78,105], fill=(255,255,255,220))
save(img, "btn_up.png")

# btn_down.png — 128x128, down arrow
img = Image.new("RGBA", (128, 128), (0,0,0,80))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,124,124], radius=16, outline=(255,255,255,200), width=3)
draw.polygon([(64,108),(28,58),(100,58)], fill=(255,255,255,220))
draw.rectangle([50,23,78,60], fill=(255,255,255,220))
save(img, "btn_down.png")

# menu_vs_ai.png — 256x64
img = Image.new("RGBA", (256, 64), (40,40,120,220))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,252,60], radius=10, outline=(200,200,255,255), width=2)
try:
    font = ImageFont.truetype("arial.ttf", 22)
except:
    font = ImageFont.load_default()
draw.text((30, 18), "VS COMPUTER", fill=(255,255,255), font=font)
save(img, "menu_vs_ai.png")

# menu_vs_bt.png — 256x64
img = Image.new("RGBA", (256, 64), (40,80,40,220))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,252,60], radius=10, outline=(150,255,150,255), width=2)
try:
    font = ImageFont.truetype("arial.ttf", 22)
except:
    font = ImageFont.load_default()
draw.text((20, 18), "VS BLUETOOTH", fill=(255,255,255), font=font)
save(img, "menu_vs_bt.png")

# gameover_p1wins.png — 512x128
img = Image.new("RGBA", (512, 128), (20,20,20,200))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,508,124], radius=12, outline=(255,200,0,255), width=3)
try:
    font = ImageFont.truetype("arial.ttf", 42)
except:
    font = ImageFont.load_default()
draw.text((80, 35), "PLAYER 1 WINS!", fill=(255,200,0), font=font)
save(img, "gameover_p1wins.png")

# gameover_p2wins.png — 512x128
img = Image.new("RGBA", (512, 128), (20,20,20,200))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle([4,4,508,124], radius=12, outline=(100,150,255,255), width=3)
try:
    font = ImageFont.truetype("arial.ttf", 42)
except:
    font = ImageFont.load_default()
draw.text((80, 35), "PLAYER 2 WINS!", fill=(100,150,255), font=font)
save(img, "gameover_p2wins.png")

# explosion_sound.wav — short explosion sound effect (procedural)
import struct, wave, math
sample_rate = 22050
duration = 0.4  # seconds
n_samples = int(sample_rate * duration)
samples = []
import random as rnd
rnd.seed(777)
for i in range(n_samples):
    t = i / sample_rate
    # Envelope: sharp attack, fast decay
    env = max(0.0, 1.0 - t / duration) ** 2
    # Mix of noise + low rumble
    noise = rnd.uniform(-1.0, 1.0)
    rumble = math.sin(2 * math.pi * 60 * t) * 0.5
    crackle = math.sin(2 * math.pi * 200 * t) * 0.3
    sample = (noise * 0.6 + rumble + crackle) * env * 0.8
    sample = max(-1.0, min(1.0, sample))
    samples.append(int(sample * 32767))

wav_path = os.path.join(ASSETS_DIR, "explosion_sound.wav")
with wave.open(wav_path, 'w') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(sample_rate)
    wf.writeframes(struct.pack(f'<{len(samples)}h', *samples))
print(f"Created: explosion_sound.wav")

print("\nDone! All assets created.")
