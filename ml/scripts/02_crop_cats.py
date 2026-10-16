#!/usr/bin/env python3
"""
Crop the cat out of each frame with YOLO, so embeddings see the cat, not the room.

    02_crop_cats.py --input data/frames    --output data/crops/own
    02_crop_cats.py --input data/other_raw --output data/crops/other
"""
import argparse
from pathlib import Path

from PIL import Image
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent.parent
CAT_CLASS = 15  # COCO class index for 'cat'
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp"}


def iter_images(root: Path):
    for p in root.rglob("*"):
        if p.suffix.lower() in IMG_EXTS:
            yield p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="folder with frames (subfolders allowed)")
    ap.add_argument("--output", required=True, help="where to save cat crops")
    ap.add_argument("--conf", type=float, default=0.4, help="detector confidence threshold")
    ap.add_argument("--pad", type=float, default=0.1, help="margin around the box (fraction)")
    ap.add_argument("--min-size", type=int, default=96, help="min crop side, px")
    args = ap.parse_args()

    in_dir = (ROOT / args.input) if not Path(args.input).is_absolute() else Path(args.input)
    out_dir = (ROOT / args.output) if not Path(args.output).is_absolute() else Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    imgs = list(iter_images(in_dir))
    if not imgs:
        print(f"No images in {in_dir}")
        return
    print("Loading YOLO... (downloads ~6 MB the first time)")
    weights = ROOT / "yolov8n.pt"
    model = YOLO(str(weights) if weights.exists() else "yolov8n.pt")

    saved = 0
    for i, img_path in enumerate(imgs):
        res = model(str(img_path), classes=[CAT_CLASS], conf=args.conf, verbose=False)[0]
        if res.boxes is None or len(res.boxes) == 0:
            continue
        im = Image.open(img_path).convert("RGB")
        W, H = im.size
        # take the largest cat box in the frame
        best = max(res.boxes, key=lambda b: (b.xyxy[0][2] - b.xyxy[0][0]) * (b.xyxy[0][3] - b.xyxy[0][1]))
        x1, y1, x2, y2 = [float(v) for v in best.xyxy[0]]
        pw, ph = (x2 - x1) * args.pad, (y2 - y1) * args.pad
        x1, y1 = max(0, x1 - pw), max(0, y1 - ph)
        x2, y2 = min(W, x2 + pw), min(H, y2 + ph)
        if min(x2 - x1, y2 - y1) < args.min_size:
            continue
        crop = im.crop((int(x1), int(y1), int(x2), int(y2)))
        # frames of different videos share names, so keep the video folder in the crop name
        crop.save(out_dir / f"{img_path.parent.name}_{img_path.stem}_cat.jpg", quality=92)
        saved += 1
        if (i + 1) % 50 == 0:
            print(f"  processed {i+1}/{len(imgs)}, crops {saved}")

    print(f"\nDone: {saved} cat crops → {out_dir}")
    print("Next: 03_embed.py — compute DINOv2 embeddings.")


if __name__ == "__main__":
    main()
