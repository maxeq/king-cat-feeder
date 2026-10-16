#!/usr/bin/env bash
# PreToolUse (Edit/Write): refuse edits to generated or secret files. Exit 2 = blocked, stderr goes to Claude.
path=$(python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("tool_input",{}).get("file_path",""))')
case "$path" in
  */src/net/pages.h)
    echo "pages.h is generated: edit firmware/king_board/web/*.html and run python3 firmware/king_board/web/build.py" >&2
    exit 2 ;;
  */secrets.h)
    echo "secrets.h holds real credentials and is the owner's to edit. Use secrets.h.example for the template." >&2
    exit 2 ;;
esac
exit 0
