#!/usr/bin/env python3
"""
DINOv2 ViT-S/14 embeddings for a folder of crops, L2-normalised, saved as .npz.

    03_embed.py --input data/crops/own --out embeddings/own.npz
"""
import argparse
from pathlib import Path

import numpy as np
import timm
import torch
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
MODEL_NAME = "vit_small_patch14_dinov2.lvd142m"
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp"}


def device():
    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="folder with cat crops")
    ap.add_argument("--out", required=True, help="where to save the embeddings .npz")
    args = ap.parse_args()

    in_dir = (ROOT / args.input) if not Path(args.input).is_absolute() else Path(args.input)
    out_path = (ROOT / args.out) if not Path(args.out).is_absolute() else Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    files = sorted([p for p in in_dir.rglob("*") if p.suffix.lower() in IMG_EXTS])
    if not files:
        print(f"No crops in {in_dir}")
        return

    dev = device()
    print(f"Device: {dev}. Loading DINOv2 ({MODEL_NAME})... (downloads ~85 MB the first time)")
    model = timm.create_model(MODEL_NAME, pretrained=True, num_classes=0)  # 0 → returns the embedding
    model.eval().to(dev)
    cfg = timm.data.resolve_model_data_config(model)
    transform = timm.data.create_transform(**cfg, is_training=False)

    embs, names = [], []
    with torch.no_grad():
        for i, f in enumerate(files):
            img = Image.open(f).convert("RGB")
            x = transform(img).unsqueeze(0).to(dev)
            e = model(x)[0].float().cpu().numpy()
            e = e / (np.linalg.norm(e) + 1e-9)  # L2 normalization
            embs.append(e)
            names.append(f.name)
            if (i + 1) % 50 == 0:
                print(f"  {i+1}/{len(files)}")

    embs = np.stack(embs).astype(np.float32)
    np.savez(out_path, embeddings=embs, names=np.array(names))
    print(f"\nDone: {len(embs)} embeddings of dim {embs.shape[1]} → {out_path}")
    print("Next: 04_evaluate.py — FAR/FRR curve.")


if __name__ == "__main__":
    main()
