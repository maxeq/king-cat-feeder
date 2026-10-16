#!/usr/bin/env bash
# PreToolUse (Bash): before Claude runs `git commit`, block private files and failing checks.
# If the git pre-commit hook is installed (scripts/setup-dev.sh), git runs the checks itself.
cmd=$(python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("tool_input",{}).get("command",""))')
case "$cmd" in *"git commit"*) ;; *) exit 0 ;; esac
root=${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel)}
cd "$root" || exit 0
if git diff --cached --name-only | grep -qE '(^|/)secrets\.h$|demo_photos\.json$|\.(step|stp|kicad_pcb|kicad_sch|gbr)$'; then
  echo "blocked: a private file is staged (secrets.h, photos, CAD or PCB)" >&2
  exit 2
fi
[ "$(git config core.hooksPath)" = ".githooks" ] && exit 0
out=$(scripts/check.sh 2>&1) || { echo "blocked: checks failed" >&2; echo "$out" >&2; exit 2; }
exit 0
