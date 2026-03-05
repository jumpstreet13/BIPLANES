from PIL import Image, ImageDraw
import os, math

ASSETS = "app/src/main/assets"
os.makedirs(ASSETS, exist_ok=True)

def save(img, name):
    img.save(os.path.join(ASSETS, name))
    print(f"  {name}")

print("Creating biplane sprites...")

# plane_p1.png — 64x32, red-yellow biplane facing right
img = Image.new("RGBA", (64, 32), (0,0,0,0))
d = ImageDraw.Draw(img)
# Fuselage
d.rectangle([12, 13, 50, 19], fill=(220, 60, 40))
# Nose/cowl
d.rectangle([50, 12, 60, 20], fill=(180, 40, 20))
# Propeller
d.line([(62, 8), (62, 24)], fill=(80,80,80), width=2)
# Tail
d.polygon([(12,13),(2,8),(12,10)], fill=(180,40,20))
d.polygon([(12,19),(2,24),(12,22)], fill=(180,40,20))
# Upper wing
d.rectangle([22, 6, 44, 10], fill=(240, 200, 0))
# Lower wing
d.rectangle([22, 22, 44, 26], fill=(240, 200, 0))
# Wing struts
d.line([(26, 10), (26, 22)], fill=(100,60,0), width=1)
d.line([(40, 10), (40, 22)], fill=(100,60,0), width=1)
# Cockpit
d.ellipse([36, 11, 48, 21], fill=(140, 200, 255, 200))
save(img, "plane_p1.png")

# plane_p2.png — 64x32, blue-green biplane facing left
img = Image.new("RGBA", (64, 32), (0,0,0,0))
d = ImageDraw.Draw(img)
# Fuselage
d.rectangle([14, 13, 52, 19], fill=(40, 80, 220))
# Nose/cowl (left)
d.rectangle([4, 12, 14, 20], fill=(20, 50, 180))
# Propeller
d.line([(2, 8), (2, 24)], fill=(80,80,80), width=2)
# Tail (right)
d.polygon([(52,13),(62,8),(52,10)], fill=(20,50,180))
d.polygon([(52,19),(62,24),(52,22)], fill=(20,50,180))
# Upper wing
d.rectangle([20, 6, 42, 10], fill=(40, 200, 100))
# Lower wing
d.rectangle([20, 22, 42, 26], fill=(40, 200, 100))
# Struts
d.line([(24, 10), (24, 22)], fill=(0,80,40), width=1)
d.line([(38, 10), (38, 22)], fill=(0,80,40), width=1)
# Cockpit
d.ellipse([16, 11, 28, 21], fill=(140, 200, 255, 200))
save(img, "plane_p2.png")

print("Creating explosion frames...")

# explosion_0..4 — 48x48, explosion animation
explosion_colors = [
    [(255,200,0), (255,150,0)],   # frame 0: small yellow
    [(255,150,0), (255,80,0)],    # frame 1: orange
    [(255,80,0),  (200,0,0)],     # frame 2: red big
    [(180,0,0),   (120,60,60)],   # frame 3: dark
    [(100,60,60), (60,60,60)],    # frame 4: smoke
]
radii = [8, 14, 20, 18, 16]

for i, (colors, r) in enumerate(zip(explosion_colors, radii)):
    img = Image.new("RGBA", (48, 48), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = 24, 24
    # Outer glow
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(*colors[0], 200))
    # Core
    core = max(2, r - 5)
    d.ellipse([cx-core, cy-core, cx+core, cy+core], fill=(*colors[1], 240))
    # Sparks (only first 3 frames)
    if i < 3:
        for angle_deg in range(0, 360, 45):
            a = math.radians(angle_deg)
            sx = cx + int((r+4) * math.cos(a))
            sy = cy + int((r+4) * math.sin(a))
            d.ellipse([sx-2, sy-2, sx+2, sy+2], fill=(*colors[0], 200))
    save(img, f"explosion_{i}.png")

print("Done!")
