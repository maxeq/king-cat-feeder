#!/usr/bin/env python3
"""
Split videos in data/videos/ into frames, 2 fps by default.
At 30 fps neighbouring frames are near-duplicates and inflate the scores.

    01_extract_frames.py [--fps 1]
"""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VIDEOS = ROOT / "data" / "videos"
FRAMES = ROOT / "data" / "frames"
EXTS = {".mp4", ".mov", ".m4v", ".avi", ".mkv"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fps", type=float, default=2.0, help="frames per second")
    args = ap.parse_args()

    videos = [p for p in VIDEOS.iterdir() if p.suffix.lower() in EXTS] if VIDEOS.exists() else []
    if not videos:
        print(f"No videos in {VIDEOS}. Put cat videos (.mp4/.mov) there and run again.")
        return

    for v in videos:
        out = FRAMES / v.stem
        out.mkdir(parents=True, exist_ok=True)
        print(f"Splitting {v.name} → {out}/  ({args.fps} fps)")
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-i", str(v),
            "-vf", f"fps={args.fps}",
            "-q:v", "2",
            str(out / "frame_%05d.jpg"),
        ], check=True)
        n = len(list(out.glob("*.jpg")))
        print(f"  done: {n} frames")

    print("\nNext: 02_crop_cats.py — crop the cat out of the frames.")


if __name__ == "__main__":
    main()
