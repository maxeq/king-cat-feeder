#!/usr/bin/env bash
# PostToolUse (Edit/Write): keep the tree in the shape check.sh expects.
#   C++ in the firmware  -> clang-format in place
#   web/*.html           -> rebuild src/net/pages.h
root=${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel)}
path=$(python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("tool_input",{}).get("file_path",""))')
[ -f "$path" ] || exit 0
cf="$root/.venv-tools/bin/clang-format"; [ -x "$cf" ] || cf=$(command -v clang-format)
case "$path" in
  */firmware/king_board/*.cpp|*/firmware/king_board/*.h)
    [ "${path##*/}" = pages.h ] && exit 0
    [ -n "$cf" ] && (cd "$root/firmware/king_board" && "$cf" -i "$path") ;;
  */firmware/king_board/*.ino)
    if [ -n "$cf" ]; then tmp=$(mktemp); "$cf" --assume-filename=x.cpp < "$path" > "$tmp" && cat "$tmp" > "$path"; rm -f "$tmp"; fi ;;
  */firmware/king_board/web/*.html|*/firmware/king_board/web/build.py)
    out=$(python3 "$root/firmware/king_board/web/build.py" 2>&1) || { echo "web bundle failed: $out" >&2; exit 2; } ;;
esac
exit 0
