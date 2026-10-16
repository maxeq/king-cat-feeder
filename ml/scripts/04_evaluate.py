#!/usr/bin/env python3
"""
Does our cat separate from the others? Enroll half of our crops, score the rest and
the other cats by max cosine similarity, sweep the threshold, report EER/FAR/FRR.
Use --split videos to enroll from some videos and test on others; splitting
frames of the same video lets near-identical neighbours inflate the result.

    04_evaluate.py --own embeddings/own.npz --other embeddings/other.npz --split videos
"""
import argparse
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent


def load(npz_path):
    d = np.load(npz_path, allow_pickle=True)
    return d["embeddings"].astype(np.float32), d["names"]


def video_of(name):
    """Crop names are <video>_frame_<n>_cat.jpg; anything else counts as its own source."""
    return name.rsplit("_frame_", 1)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--own", required=True, help=".npz with our cat's embeddings")
    ap.add_argument("--other", required=True, help=".npz with other cats' embeddings")
    ap.add_argument("--enroll-frac", type=float, default=0.5,
                    help="fraction of own crops (or videos) used for enrollment")
    ap.add_argument("--split", choices=["frames", "videos"], default="frames",
                    help="split own crops by frame order or by source video")
    ap.add_argument("--plot", default="results/det_curve.png", help="where to save the plot")
    args = ap.parse_args()

    own_p = (ROOT / args.own) if not Path(args.own).is_absolute() else Path(args.own)
    other_p = (ROOT / args.other) if not Path(args.other).is_absolute() else Path(args.other)
    own, own_names = load(own_p)
    other, _ = load(other_p)

    # Deterministic split into enrollment/test (no random, so runs are reproducible).
    n = len(own)
    if args.split == "videos":
        videos = sorted({video_of(str(x)) for x in own_names})
        if len(videos) < 2:
            print("--split videos needs crops from at least two videos.")
            return
        enroll_videos = set(videos[:max(1, int(len(videos) * args.enroll_frac))])
        mask = np.array([video_of(str(x)) in enroll_videos for x in own_names])
        enroll, own_test = own[mask], own[~mask]
        print(f"Videos: {len(videos)} (enrollment {len(enroll_videos)})")
    else:
        k = max(1, int(n * args.enroll_frac))
        enroll, own_test = own[:k], own[k:]
    print(f"Own crops: {n} (enrollment {len(enroll)}, test {len(own_test)})")
    print(f"Other crops: {len(other)}")
    if len(own_test) == 0:
        print("Too few own-cat crops for testing — need more frames.")
        return

    # Similarity of a test vector to the reference = max cosine over enrollment vectors
    # (embeddings are already L2-normalized, so the dot product = cosine).
    def max_sim(x):
        return (x @ enroll.T).max(axis=1)

    own_scores = max_sim(own_test)     # should be HIGH for own
    other_scores = max_sim(other)      # should be LOW for others

    # Sweep the threshold, compute FAR/FRR
    thresholds = np.linspace(0.0, 1.0, 201)
    far = np.array([(other_scores >= t).mean() for t in thresholds])  # other accepted
    frr = np.array([(own_scores < t).mean() for t in thresholds])     # own rejected

    eer_i = np.argmin(np.abs(far - frr))
    eer = (far[eer_i] + frr[eer_i]) / 2
    print("\n=== Separability result ===")
    print(f"EER (equal error rate): {eer*100:.1f}%   at threshold {thresholds[eer_i]:.3f}")
    print(f"  at this threshold: FAR {far[eer_i]*100:.1f}% (other let in), "
          f"FRR {frr[eer_i]*100:.1f}% (own refused)")
    # "Low FAR" operating point: keep others out at the cost of refusing our own
    for target in (0.01, 0.05):
        ok = np.where(far <= target)[0]
        if len(ok):
            j = ok[0]
            print(f"  at FAR≈{target*100:.0f}%: threshold {thresholds[j]:.3f}, "
                  f"FRR {frr[j]*100:.1f}% (how often our cat is denied food)")
    print(f"\nMean similarity: own {own_scores.mean():.3f}, other {other_scores.mean():.3f}")
    print("The larger the gap between these numbers, the better the cat separates.")

    # Plot
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        out = (ROOT / args.plot) if not Path(args.plot).is_absolute() else Path(args.plot)
        out.parent.mkdir(parents=True, exist_ok=True)
        plt.figure(figsize=(6, 5))
        plt.plot(thresholds, far * 100, label="FAR — other let in")
        plt.plot(thresholds, frr * 100, label="FRR — own refused")
        plt.axvline(thresholds[eer_i], color="gray", ls="--", lw=0.8)
        plt.xlabel("similarity threshold")
        plt.ylabel("error, %")
        plt.title(f"king: cat separability (EER {eer*100:.1f}%)")
        plt.legend()
        plt.grid(alpha=0.3)
        plt.tight_layout()
        plt.savefig(out, dpi=120)
        print(f"Plot: {out}")
    except Exception as e:
        print(f"(plot not generated: {e})")


if __name__ == "__main__":
    main()
