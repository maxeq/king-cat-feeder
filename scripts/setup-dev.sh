#!/usr/bin/env bash
# One-time dev setup: pinned formatter, static analyser, linter and test runner in .venv-tools,
# plus the git pre-commit hook, which is the project's gate (there is no CI).
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m venv .venv-tools
.venv-tools/bin/pip install -q --upgrade pip
.venv-tools/bin/pip install -q clang-format==23.1.3 clang-tidy==22.1.8 ruff==0.17.1 pytest numpy==2.5.1 \
  cryptography==50.0.2  # cryptography: signing OTA images (scripts/ota-key.sh, scripts/ota.sh)
git config core.hooksPath .githooks
echo "ok: tools in .venv-tools, pre-commit hook on (.githooks/pre-commit)"
