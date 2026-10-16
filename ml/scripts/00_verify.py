#!/usr/bin/env python3
"""
Check the environment and pre-download the DINOv2 and YOLO weights.
"""
import numpy as np
import PIL.Image
import timm
import torch
from ultralytics import YOLO

print("torch:", torch.__version__)
print("MPS (Apple Silicon acceleration):", torch.backends.mps.is_available())
dev = "mps" if torch.backends.mps.is_available() else "cpu"

print("\nLoading DINOv2 (downloads ~85 MB on first run)...")
m = timm.create_model("vit_small_patch14_dinov2.lvd142m", pretrained=True, num_classes=0)
m.eval().to(dev)
cfg = timm.data.resolve_model_data_config(m)
tf = timm.data.create_transform(**cfg, is_training=False)
# run a random image → check embedding size
dummy = PIL.Image.fromarray((np.random.rand(300, 300, 3) * 255).astype("uint8"))
with torch.no_grad():
    emb = m(tf(dummy).unsqueeze(0).to(dev))
print("DINOv2 ok, embedding shape:", tuple(emb.shape), "→ vector of", emb.shape[1], "values")

print("\nLoading YOLOv8n (downloads ~6 MB)...")
YOLO("yolov8n.pt")
print("YOLO ok.")

print("\n=== Ready for inference. ===")
