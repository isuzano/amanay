#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

patterns=(
  '^[[:space:]]*//[[:space:]]*Forward declarations([[:space:]]|$)'
  '^[[:space:]]*//[[:space:]]*Private helpers([[:space:]]|$)'
  '^[[:space:]]*//[[:space:]]*Private callbacks([[:space:]]|$)'
  '^[[:space:]]*//[[:space:]]*Public API[[:space:]]*-[[:space:]]*'
  '^[[:space:]]*/\*[[:space:]]*Forward declarations([[:space:]]|$)'
  '^[[:space:]]*/\*[[:space:]]*Private helpers([[:space:]]|$)'
  '^[[:space:]]*/\*[[:space:]]*Public API[[:space:]]*-[[:space:]]*'
)

status=0
files="$(find src include tests -type f \( -name '*.c' -o -name '*.h' \))"

for p in "${patterns[@]}"; do
  if grep -nE "$p" $files >/tmp/lds-comment-policy-match.txt 2>/dev/null; then
    if [[ $status -eq 0 ]]; then
      echo "Comment policy violations found:"
    fi
    cat /tmp/lds-comment-policy-match.txt
    status=1
  fi
done

rm -f /tmp/lds-comment-policy-match.txt

if [[ $status -ne 0 ]]; then
  cat <<'EOF'

Please replace decorative comments with either:
- gtk-doc comments for exported API, or
- short, useful internal comments explaining non-obvious behavior.
EOF
  exit 1
fi

echo "Comment policy check passed."
