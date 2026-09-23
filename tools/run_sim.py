#!/usr/bin/env python3
"""One command PC test: build the simulator, run a scripted ride, make a GIF.

    python3 tools/run_sim.py                 # -> sim_out/demo.gif + page PNGs
    python3 tools/run_sim.py --open          # ...and open the GIF afterwards
    python3 tools/run_sim.py --seconds 20    # shorter ride

Needs: a C++ compiler (g++ / clang++ / MinGW on Windows) or PlatformIO, and
Python with Pillow (pip install pillow). Nothing else: no board, no VESC.

The simulator runs the real firmware (src/main.cpp, src/Dashboard.cpp and the
VescUart library) against stand-ins for Arduino and TFT_eSPI that live in
src/sim, using the original display fonts, so what you see is what the board
will draw.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import webbrowser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT, ".sim_build")

SOURCES = (
    glob.glob(os.path.join(ROOT, "src", "*.cpp"))
    + glob.glob(os.path.join(ROOT, "src", "sim", "*.cpp"))
    + [os.path.join(ROOT, "src", "sim", "fonts", "fonts.cpp")]
    + glob.glob(os.path.join(ROOT, "lib", "VescUart", "*.cpp"))
)
INCLUDES = [os.path.join(ROOT, d) for d in ("src/sim", "include", "lib/VescUart", "src")]


def find_compiler():
    for cxx in (os.environ.get("CXX"), "g++", "clang++", "c++"):
        if cxx and shutil.which(cxx):
            return shutil.which(cxx)
    return None


def build():
    os.makedirs(BUILD_DIR, exist_ok=True)
    exe = os.path.join(BUILD_DIR, "vesc_sim.exe" if os.name == "nt" else "vesc_sim")
    cxx = find_compiler()
    if cxx:
        cmd = [cxx, "-std=c++17", "-O2"] + [f"-I{d}" for d in INCLUDES] + SOURCES + ["-o", exe]
        print("building with", os.path.basename(cxx))
        subprocess.check_call(cmd)
        return exe
    if shutil.which("pio"):
        print("no C++ compiler found, building with PlatformIO")
        subprocess.check_call(["pio", "run", "-e", "sim"], cwd=ROOT)
        built = os.path.join(ROOT, ".pio", "build", "sim", "program" + (".exe" if os.name == "nt" else ""))
        shutil.copy(built, exe)
        return exe
    sys.exit(
        "No C++ compiler found. Install one of:\n"
        "  Windows: MSYS2/MinGW (pacman -S mingw-w64-ucrt-x86_64-gcc) or PlatformIO\n"
        "  macOS:   xcode-select --install\n"
        "  Linux:   sudo apt install g++"
    )


def make_gif(frames_dir, out_dir, fps, scale):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required for the GIF: pip install pillow")

    frames = sorted(glob.glob(os.path.join(frames_dir, "frame_*.ppm")))
    if not frames:
        sys.exit("simulator produced no frames")
    images = []
    for f in frames:
        im = Image.open(f).convert("RGB")
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        images.append(im.quantize(colors=64, method=Image.Quantize.MEDIANCUT))

    gif = os.path.join(out_dir, "demo.gif")
    images[0].save(gif, save_all=True, append_images=images[1:], duration=int(1000 / fps), loop=0, optimize=True)

    # Still images of each page, taken at moments the script has settled.
    stills = {"page-boot.png": 1.0, "page-main.png": 10.0, "page-stats.png": 15.0, "page-trip.png": 21.0,
              "page-gforce.png": 27.0, "page-battery.png": 33.0, "page-gear.png": 37.9,
              "page-settings.png": 41.5, "page-settings2.png": 45.0, "page-fault.png": 49.0, "page-lowbatt.png": 52.5,
              "page-nolink.png": 56.5}
    for name, t in stills.items():
        idx = min(int(t * fps), len(frames) - 1)
        im = Image.open(frames[idx]).convert("RGB")
        im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(os.path.join(out_dir, name))
    return gif, len(frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "sim_out"), help="output directory")
    ap.add_argument("--seconds", type=float, default=57.0, help="length of the ride")
    ap.add_argument("--fps", type=int, default=8)
    ap.add_argument("--scale", type=int, default=2, help="upscale factor for the GIF")
    ap.add_argument("--open", action="store_true", help="open the GIF when done")
    args = ap.parse_args()

    exe = build()
    frames_dir = os.path.join(args.out, "frames")
    shutil.rmtree(frames_dir, ignore_errors=True)
    os.makedirs(frames_dir, exist_ok=True)
    subprocess.check_call([exe, "--out", frames_dir, "--seconds", str(args.seconds), "--fps", str(args.fps), "--quiet"])

    gif, n = make_gif(frames_dir, args.out, args.fps, args.scale)
    print(f"{n} frames -> {gif}")
    if args.open:
        webbrowser.open("file://" + os.path.abspath(gif))


if __name__ == "__main__":
    main()
