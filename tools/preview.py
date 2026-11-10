"""Build the preview program and draw every screen on this computer, for checking by eye before flashing.

    python tools/preview.py                       # preview/out/sheet.png: each animation on each palette color, and the states
    python tools/preview.py gif snow ff0000 100   # preview/out/snow-ff0000.gif: 100 frames at 25 a second

Needs g++ and Pillow. Each image is drawn at 2x, so single pixels stay visible.
"""
import glob
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "preview", "out")
EXE = os.path.join(OUT, "preview.exe" if os.name == "nt" else "preview")


def build():
    os.makedirs(OUT, exist_ok=True)
    sources = [os.path.join(ROOT, "tools", "preview", "preview.cpp")] + glob.glob(os.path.join(ROOT, "lib", "screen", "*.cpp")) + [os.path.join(ROOT, "lib", "profile", "profile.cpp")]
    subprocess.run(["g++", "-std=gnu++17", "-O2", "-Wall", "-I", os.path.join(ROOT, "lib", "screen"), "-I", os.path.join(ROOT, "lib", "profile"), *sources, "-o", EXE], check=True)


def run(*args, where):
    os.makedirs(where, exist_ok=True)
    for f in glob.glob(os.path.join(where, "*.ppm")):
        os.remove(f)
    subprocess.run([EXE, where, *args], check=True)
    return sorted(glob.glob(os.path.join(where, "*.ppm")))


def sheet():
    files = run("stills", where=os.path.join(OUT, "stills"))
    images = [Image.open(f).resize((480, 270), Image.NEAREST) for f in files]
    for f, im in zip(files, images):
        im.save(f[:-4] + ".png")
    cols = 4
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGB", (cols * 490 + 10, rows * 280 + 10), (60, 60, 60))
    for i, im in enumerate(images):
        out.paste(im, (10 + (i % cols) * 490, 10 + (i // cols) * 280))
    path = os.path.join(OUT, "sheet.png")
    out.save(path)
    print(path, len(images), "screens")


def gif(scene, color, count):
    files = run("frames", scene, color, str(count), where=os.path.join(OUT, "frames"))
    frames = [Image.open(f).resize((480, 270), Image.NEAREST) for f in files]
    path = os.path.join(OUT, f"{scene}-{color}.gif")
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=40, loop=0)
    print(path, len(frames), "frames")


if __name__ == "__main__":
    build()
    if len(sys.argv) > 1 and sys.argv[1] == "gif":
        gif(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    else:
        sheet()
