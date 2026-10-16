#!/usr/bin/env python3
"""
Like 02, but crops the face with OpenCV's cat-face Haar cascade.
The bowl camera mostly sees the face. Only frontal faces are found.

    02b_crop_faces.py --input data/frames --output data/crops_face/own
"""
import argparse
from pathlib import Path

import cv2

ROOT = Path(__file__).resolve().parent.parent
CASCADE = ROOT / "models" / "haarcascade_frontalcatface_extended.xml"
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--pad", type=float, default=0.25, help="margin around the face (fraction)")
    ap.add_argument("--min-size", type=int, default=48)
    args = ap.parse_args()

    in_dir = (ROOT / args.input) if not Path(args.input).is_absolute() else Path(args.input)
    out_dir = (ROOT / args.output) if not Path(args.output).is_absolute() else Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    cascade = cv2.CascadeClassifier(str(CASCADE))
    if cascade.empty():
        print("Cascade failed to load:", CASCADE)
        return

    files = [p for p in in_dir.rglob("*") if p.suffix.lower() in IMG_EXTS]
    saved = 0
    for i, f in enumerate(files):
        img = cv2.imread(str(f))
        if img is None:
            continue
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        faces = cascade.detectMultiScale(gray, scaleFactor=1.05, minNeighbors=3,
                                         minSize=(args.min_size, args.min_size))
        if len(faces) == 0:
            continue
        x, y, w, h = max(faces, key=lambda r: r[2] * r[3])  # largest face
        px, py = int(w * args.pad), int(h * args.pad)
        H, W = img.shape[:2]
        x1, y1 = max(0, x - px), max(0, y - py)
        x2, y2 = min(W, x + w + px), min(H, y + h + py)
        crop = img[y1:y2, x1:x2]
        cv2.imwrite(str(out_dir / f"{f.parent.name}_{f.stem}_face.jpg"), crop,
                    [cv2.IMWRITE_JPEG_QUALITY, 92])
        saved += 1
        if (i + 1) % 200 == 0:
            print(f"  {i+1}/{len(files)}, faces {saved}")

    print(f"Done: {saved} faces from {len(files)} → {out_dir}")


if __name__ == "__main__":
    main()
