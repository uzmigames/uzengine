"""Convert StoneBricks GLOSS->Roughness and resize all textures to 2048 for engine use."""
from PIL import Image, ImageOps
import os

SRC = r"E:\UzEngine\assets\textures"
SIZE = 2048

def convert(src_name, dst_name, invert=False):
    path = os.path.join(SRC, src_name)
    if not os.path.exists(path):
        print(f"SKIP: {src_name} not found")
        return
    print(f"Processing {src_name} -> {dst_name}...", flush=True)
    img = Image.open(path).convert("RGB")
    if img.size[0] > SIZE or img.size[1] > SIZE:
        img = img.resize((SIZE, SIZE), Image.LANCZOS)
    if invert:
        img = ImageOps.invert(img)
    dst_path = os.path.join(SRC, dst_name)
    img.save(dst_path, quality=95)
    print(f"  Saved {dst_name} ({img.size})", flush=True)

convert("StoneBricksSplitface001_COL_4K.jpg", "stonebricks_albedo.jpg")
convert("StoneBricksSplitface001_NRM_4K.jpg", "stonebricks_normal.jpg")
convert("StoneBricksSplitface001_AO_4K.jpg", "stonebricks_ao.jpg")
convert("StoneBricksSplitface001_GLOSS_4K.jpg", "stonebricks_roughness.jpg", invert=True)
convert("StoneBricksSplitface001_REFL_4K.jpg", "stonebricks_metallic.jpg")

# Small preview for analysis
prev = os.path.join(SRC, "StoneBricksSplitface001_Preview1.png")
if os.path.exists(prev):
    img = Image.open(prev).convert("RGB").resize((512, 512), Image.LANCZOS)
    out = r"C:\Users\Bolado\.gemini\antigravity\brain\d501b827-2b7c-4816-9b86-054789fdd170\preview_reference.png"
    img.save(out)
    print("Preview saved", flush=True)

print("DONE", flush=True)
