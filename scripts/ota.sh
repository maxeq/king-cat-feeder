#!/usr/bin/env bash
# Build, sign and upload the firmware over Wi-Fi:
#   scripts/ota.sh <board-ip | kingboard.local>
# The board takes only images signed with the key from scripts/ota-key.sh, and asks for
# SECRET_OTA_PASS (read from secrets.h, or KING_OTA_PASS). A plain `arduino-cli upload`
# over the network sends an unsigned image, which the board rejects at the end.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST=${1:?usage: scripts/ota.sh <board-ip | kingboard.local>}
FW=firmware/king_board
KEY=${KING_OTA_KEY:-$HOME/.config/king-board/ota_signing_key.pem}
PY=.venv-tools/bin/python
[ -f "$KEY" ] || { echo "no signing key at $KEY: run scripts/ota-key.sh once"; exit 1; }
[ -x "$PY" ] || { echo "run scripts/setup-dev.sh first"; exit 1; }
PASS=${KING_OTA_PASS:-$(sed -n 's/^#define SECRET_OTA_PASS *"\(.*\)".*/\1/p' $FW/secrets.h)}
[ -n "$PASS" ] || { echo "no OTA password: set SECRET_OTA_PASS in $FW/secrets.h"; exit 1; }
CORE="$(arduino-cli config get directories.data)/packages/esp32/hardware/esp32/3.3.10"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

arduino-cli compile --profile king_board \
  --build-property "compiler.cpp.extra_flags=-DFW_GIT=\"$(git rev-parse --short HEAD)\"" \
  --output-dir "$OUT" $FW
"$PY" "$CORE/tools/bin_signing.py" --bin "$OUT/king_board.ino.bin" --key "$KEY" --out "$OUT/signed.bin"
python3 "$CORE/tools/espota.py" -i "$HOST" -a "$PASS" -f "$OUT/signed.bin" -r
