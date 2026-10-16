"""04_evaluate.py on synthetic embeddings: a tight "own" cluster vs random "others"."""
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "04_evaluate.py"


def unit(x):
    return (x / np.linalg.norm(x, axis=1, keepdims=True)).astype(np.float32)


@pytest.fixture
def embeddings(tmp_path):
    rng = np.random.default_rng(0)
    center = rng.normal(size=384)
    own = unit(center + 0.3 * rng.normal(size=(40, 384)))
    other = unit(rng.normal(size=(30, 384)))
    names = [f"vid{i // 20}_frame_{i:05d}_cat.jpg" for i in range(40)]
    np.savez(tmp_path / "own.npz", embeddings=own, names=np.array(names))
    np.savez(tmp_path / "other.npz", embeddings=other, names=np.array([f"o{i}.jpg" for i in range(30)]))
    return tmp_path


def run(tmp, *extra):
    return subprocess.run(
        [sys.executable, str(SCRIPT), "--own", str(tmp / "own.npz"), "--other", str(tmp / "other.npz"),
         "--plot", str(tmp / "det.png"), *extra],
        capture_output=True, text=True, check=True).stdout


@pytest.mark.parametrize("split", ["frames", "videos"])
def test_separable_clusters_give_low_eer(embeddings, split):
    out = run(embeddings, "--split", split)
    eer = float(out.split("EER (equal error rate): ")[1].split("%")[0])
    assert eer < 5


def test_video_split_keeps_videos_apart(embeddings):
    out = run(embeddings, "--split", "videos")
    assert "Videos: 2 (enrollment 1)" in out
