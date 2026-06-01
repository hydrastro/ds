#!/usr/bin/env bash
# Remove build artifacts and legacy junk so the repo is clean to push.
#
# Default mode is a dry run. Pass --apply to actually remove.
# Tracked files are removed with `git rm` (staged + reversible); untracked
# files are removed from disk. Run from the repository root.
#
#   ./clean.sh            # preview
#   ./clean.sh --apply    # do it

set -euo pipefail

apply=0
for arg in "$@"; do
  case "$arg" in
    --apply) apply=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Refuse to run anywhere that isn't this project's root.
[ -f Makefile ] && [ -d src ] || {
  echo "error: run from the repo root (Makefile and src/ not found here)" >&2
  exit 2
}
in_git=0
git rev-parse --is-inside-work-tree >/dev/null 2>&1 && in_git=1

# Paths considered junk: build output, vendored dep (now an external flake
# input), and dead migration/scratch files.
JUNK=(
  build
  vendor
  expl
  run_tests.sh
  scripts/clean-tree.sh
  src/main.c          # moved to app/tpcas.c
  tpcas tpcas3 test_rt
  src.zip tpcas_ds_refactor.zip tpcas_ds_refactor
)

remove() {
  local p="$1"
  [ -e "$p" ] || return 0
  if [ "$in_git" -eq 1 ] && git ls-files --error-unmatch "$p" >/dev/null 2>&1; then
    # Tracked: stage the removal.
    if [ "$apply" -eq 1 ]; then
      git rm -r --quiet -- "$p" && echo "git rm  $p"
    else
      echo "would git rm  $p"
    fi
  else
    # Untracked on disk.
    if [ "$apply" -eq 1 ]; then
      rm -rf -- "$p" && echo "rm      $p"
    else
      echo "would rm      $p"
    fi
  fi
}

for p in "${JUNK[@]}"; do remove "$p"; done

# Stray root-level object files from the old flat build.
for f in ./*.o; do [ -e "$f" ] && remove "${f#./}"; done

if [ "$apply" -eq 0 ]; then
  echo
  echo "Dry run. Re-run with --apply to remove. Tracked files are staged via git rm;"
  echo "commit afterwards to finish."
fi
