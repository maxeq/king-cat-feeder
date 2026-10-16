#!/usr/bin/env bash
# Every check the project has (there is no CI). Run by the git pre-commit hook (with --build
# when the firmware changes), by Claude's hooks, and by the review agents.
#   scripts/check.sh           format, web bundle, logic tests, Python lint and tests  (~5 s)
#   scripts/check.sh --build   ... plus the firmware build
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
TOOLS=$ROOT/.venv-tools/bin
tool() { if [ -x "$TOOLS/$1" ]; then echo "$TOOLS/$1"; elif command -v "$1" >/dev/null; then command -v "$1"; fi; }
CLANG_FORMAT=$(tool clang-format); RUFF=$(tool ruff); PY=$(tool python); PY=${PY:-python3}
fail=0
step() { printf '%-22s' "$1"; }
ok() { echo "ok"; }
bad() { echo "FAIL"; [ -n "${1:-}" ] && echo "$1" | tail -20 | sed 's/^/    /'; fail=1; }

FW=firmware/king_board
step "format (C++)"
if [ -z "$CLANG_FORMAT" ]; then echo "skipped: run scripts/setup-dev.sh"; else
  out=$(cd $FW && { find src test -name '*.h' -o -name '*.cpp' | grep -v -e pages.h -e /sscma/ | xargs "$CLANG_FORMAT" --dry-run -Werror 2>&1
        "$CLANG_FORMAT" --assume-filename=king_board.cpp --dry-run -Werror < king_board.ino 2>&1; })
  [ -z "$out" ] && ok || bad "$out"
fi

step "one log"  # every line goes through core/log: it reaches /log and telnet, not only USB
out=$(grep -rn "Serial\.print" $FW/src $FW/king_board.ino | grep -v -e /sscma/ -e src/core/log.cpp -e "serial-only")
[ -z "$out" ] && ok || bad "print through say()/sayf()/warnf()/errorf(), or mark a USB-only line // serial-only:
$out"

step "web bundle"
out=$(python3 $FW/web/build.py --check 2>&1) && ok || bad "$out"

step "logic tests"
bin=$(mktemp -t king_logic.XXXXXX)
if out=$(g++ -std=c++17 -O2 -Wall -Wextra -Werror -I $FW/src $FW/test/test_logic.cpp $FW/src/logic/*.cpp -o "$bin" 2>&1) &&
   out=$("$bin" 2>&1); then echo "ok ($(echo "$out" | tail -1))"; else bad "$out"; fi

step "control tests"
if out=$(g++ -std=c++17 -O2 -Wall -Wextra -Werror -I $FW/src $FW/test/test_control.cpp $FW/src/app/controller.cpp \
           $FW/src/logic/*.cpp -o "$bin" 2>&1) &&
   out=$("$bin" 2>&1); then echo "ok ($(echo "$out" | tail -1))"; else bad "$out"; fi

step "driver tests"
if out=$(g++ -std=c++17 -O2 -Wall -Wextra -Werror -I $FW/test/fakes -I $FW/src $FW/test/test_drivers.cpp \
           $FW/test/fakes/fake_board.cpp $FW/src/drivers/servos.cpp $FW/src/drivers/button.cpp \
           $FW/src/drivers/scale.cpp $FW/src/drivers/light.cpp $FW/src/drivers/pir.cpp -o "$bin" 2>&1) &&
   out=$("$bin" 2>&1); then echo "ok ($(echo "$out" | tail -1))"; else bad "$out"; fi
rm -f "$bin"

step "static analysis"
CLANG_TIDY=$(tool clang-tidy)
if [ -z "$CLANG_TIDY" ]; then echo "skipped: run scripts/setup-dev.sh"; else
  sys=(); [ "$(uname)" = Darwin ] && sys=(-isysroot "$(xcrun --show-sdk-path)")
  out=$(cd $FW && for f in src/logic/*.cpp src/app/controller.cpp; do
          "$CLANG_TIDY" --quiet "$f" -- -std=c++17 -I src "${sys[@]}" 2>&1 | grep -E "(error|warning):" ; done)
  [ -z "$out" ] && ok || bad "$out"
fi

step "python lint"
if [ -z "$RUFF" ]; then echo "skipped: run scripts/setup-dev.sh"; else
  out=$("$RUFF" check . 2>&1) && ok || bad "$out"; fi

step "python tests"
if "$PY" -c "import pytest, numpy" 2>/dev/null; then
  out=$("$PY" -m pytest -q -p no:cacheprovider ml/tests 2>&1) && ok || bad "$out"
else echo "skipped: run scripts/setup-dev.sh"; fi

if [ "${1:-}" = "--build" ]; then
  step "firmware build"
  created=0
  [ -f $FW/secrets.h ] || { cp $FW/secrets.h.example $FW/secrets.h; created=1; }
  out=$(cd $FW && arduino-cli compile --profile king_board --warnings all --no-color . 2>&1); rc=$?
  [ $created = 1 ] && rm -f $FW/secrets.h
  warn=$(echo "$out" | grep -E "king_board/(src|king_board)[^:]*:[0-9]+:[0-9]+: (warning|error)" | grep -v /sscma/)
  if [ $rc -ne 0 ]; then bad "$out"; elif [ -n "$warn" ]; then bad "$warn"; else echo "ok ($(echo "$out" | grep -o 'Sketch uses [0-9]* bytes ([0-9]*%)'))"; fi
fi

find . -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null
exit $fail
