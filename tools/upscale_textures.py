"""Upscale the game's dumped textures into a texture pack (docs/TEXTURES.md, docs/TODO.md "Graphics upgrades").

1. Dump what the game draws:   RampageRE-dev.exe <disc> Chicago 1 --monster George --dumptex   (play a while; each city,
   the monsters you use, the menus: everything drawn once lands in mods/dump/<scope>/<w>x<h>_<hash>.png)
2. Upscale:                    python tools/upscale_textures.py [--scale 4] [--esrgan PATH] [--only Chicago]
3. Play: the game loads mods/textures/ by itself ("texture pack: N replacement textures" on its console).

Two ways to upscale, the best one found:
  - Real-ESRGAN (realesrgan-ncnn-vulkan, the AI upscaler: github.com/xinntao/Real-ESRGAN, its ncnn-vulkan release):
    --esrgan PATH to the exe, or on PATH, or the REALESRGAN environment variable. Model realesrgan-x4plus (photos and
    painted textures); x4 then reduced to --scale. Needs a Vulkan GPU.
  - Without it: Pillow, a Lanczos resize and a light unsharp mask. Better than the game's blurry stretch, not AI.
Alpha is upscaled on its own (Lanczos, then its edges kept hard where it was 0 / 255), so the see-through cut-outs
(the facade sheets, trees, fences) stay cut out. Textures already in mods/textures/ are skipped (--force redoes
them); tiny ones (under 8 px a side) and the movie frames are left alone.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_esrgan(given):
    for c in (given, os.environ.get("REALESRGAN"), shutil.which("realesrgan-ncnn-vulkan"),
              shutil.which("realesrgan-ncnn-vulkan.exe")):
        if c and os.path.isfile(c):
            return c
    return None


def split_alpha(img):
    rgba = img.convert("RGBA")
    return rgba.convert("RGB"), rgba.getchannel("A")


def upscale_alpha(a, size):
    lo, hi = a.getextrema()
    if lo == 255:
        return Image.new("L", size, 255)
    big = a.resize(size, Image.LANCZOS)
    if set(a.getdata()) <= {0, 255}:              # a cut-out: its edge kept hard, as the game's alpha test sees it
        big = big.point(lambda v: 255 if v >= 128 else 0)
    return big


def pillow_rgb(rgb, size):
    big = rgb.resize(size, Image.LANCZOS)
    return big.filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))


def esrgan_rgb(exe, rgb, size, tmp):
    src, dst = os.path.join(tmp, "in.png"), os.path.join(tmp, "out.png")
    rgb.save(src)
    r = subprocess.run([exe, "-i", src, "-o", dst, "-n", "realesrgan-x4plus", "-s", "4"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode != 0 or not os.path.isfile(dst):
        return None
    with Image.open(dst) as out:
        return out.convert("RGB").resize(size, Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=os.path.join(ROOT, "mods", "dump"))
    ap.add_argument("--out", default=os.path.join(ROOT, "mods", "textures"))
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--esrgan", help="realesrgan-ncnn-vulkan executable")
    ap.add_argument("--only", help="one scope folder of the dump (Chicago, Monsters, GUI, ...)")
    ap.add_argument("--max", type=int, default=0, help="at most this many (a try)")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    if not os.path.isdir(a.dump):
        sys.exit(f"No dump at {a.dump}: run the game with --dumptex first (docs/TEXTURES.md)")
    exe = find_esrgan(a.esrgan)
    print(f"upscaling x{a.scale} with {'Real-ESRGAN: ' + exe if exe else 'Pillow (Lanczos + sharpen); --esrgan for the AI upscaler'}")
    done = skipped = failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for scope in sorted(os.listdir(a.dump)):
            sdir = os.path.join(a.dump, scope)
            if not os.path.isdir(sdir) or (a.only and scope.lower() != a.only.lower()):
                continue
            os.makedirs(os.path.join(a.out, scope), exist_ok=True)
            for name in sorted(os.listdir(sdir)):
                if not name.lower().endswith(".png"):
                    continue
                if a.max and done >= a.max:
                    break
                dst = os.path.join(a.out, scope, name)
                if os.path.exists(dst) and not a.force:
                    skipped += 1
                    continue
                try:
                    with Image.open(os.path.join(sdir, name)) as img:
                        if min(img.size) < 8:
                            skipped += 1
                            continue
                        size = (img.size[0] * a.scale, img.size[1] * a.scale)
                        rgb, alpha = split_alpha(img)
                        big = esrgan_rgb(exe, rgb, size, tmp) if exe else None
                        if big is None:
                            big = pillow_rgb(rgb, size)
                        big.putalpha(upscale_alpha(alpha, size))
                        big.save(dst)
                        done += 1
                except Exception as e:                  # a broken dump file: left out, the rest go on
                    print(f"  {scope}/{name}: {e}")
                    failed += 1
    print(f"{done} upscaled into {a.out}, {skipped} skipped, {failed} failed")


if __name__ == "__main__":
    main()
