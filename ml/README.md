# ml

Can a pretrained model tell our cat from other cats?
YOLO finds the cat, DINOv2 turns the crop into an embedding, and the last script measures FAR/FRR.
There is no training here; it is a feasibility check before training a small model for the Grove NPU.

Needs Python 3.12 and `ffmpeg` on the PATH. Run the commands from the repository root.
Data paths are relative to `ml/`, so the videos go in `ml/data/videos/`. YOLO weights download into the current folder.

```sh
python3 -m venv ml/.venv
ml/.venv/bin/pip install -r ml/requirements.txt
ml/.venv/bin/python ml/scripts/00_verify.py      # checks the setup, downloads the weights

# ml/data/videos/     videos of your cat
# ml/data/other_raw/  photos of other cats
ml/.venv/bin/python ml/scripts/01_extract_frames.py
ml/.venv/bin/python ml/scripts/02_crop_cats.py --input data/frames    --output data/crops/own
ml/.venv/bin/python ml/scripts/02_crop_cats.py --input data/other_raw --output data/crops/other
ml/.venv/bin/python ml/scripts/03_embed.py --input data/crops/own   --out embeddings/own.npz
ml/.venv/bin/python ml/scripts/03_embed.py --input data/crops/other --out embeddings/other.npz
ml/.venv/bin/python ml/scripts/04_evaluate.py --own embeddings/own.npz --other embeddings/other.npz --split videos
```

- `--split videos` enrolls from some videos and tests on others. Splitting frames of one video inflates the result, because neighbouring frames are near-identical.
- `02b_crop_faces.py` crops faces instead of whole cats. It needs `haarcascade_frontalcatface_extended.xml` from OpenCV in `ml/models/`.
- EER under 5% means the cats separate well.
- FAR is how often another cat gets in. FRR is how often ours is refused.

Datasets and results are not in the repository.

```sh
ml/.venv/bin/pip install pytest
ml/.venv/bin/python -m pytest ml/tests    # evaluation logic on synthetic embeddings
```
