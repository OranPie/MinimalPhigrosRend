#!/usr/bin/env bash
set -euo pipefail

# Create/update a slim branch by removing heavy runtime assets.
#
# Usage:
#   scripts/ci/create_slim_branch.sh <target_branch> [source_ref] [dry-run]
#
# Examples:
#   scripts/ci/create_slim_branch.sh slim/cpp-only
#   scripts/ci/create_slim_branch.sh slim/cpp-only cpp-dev
#   scripts/ci/create_slim_branch.sh slim/cpp-only cpp-dev true

TARGET_BRANCH="${1:-}"
SOURCE_REF="${2:-${GITHUB_REF_NAME:-cpp-dev}}"
DRY_RUN="${3:-false}"

if [[ -z "${TARGET_BRANCH}" ]]; then
  echo "Usage: $0 <target_branch> [source_ref] [dry-run]" >&2
  exit 2
fi

echo "[Slim] target=${TARGET_BRANCH} source=${SOURCE_REF} dry_run=${DRY_RUN}"

git fetch --all --prune
git checkout -B "${TARGET_BRANCH}" "${SOURCE_REF}"

# Remove heavy directories (tracked + untracked safety).
git rm -r --ignore-unmatch charts phic_renderer assets/charts || true
rm -rf charts phic_renderer assets/charts

if git diff --cached --quiet && git diff --quiet; then
  echo "[Slim] No changes to commit."
  exit 0
fi

git add -A
git commit -m "chore(repo): slim clone branch without charts/phic_renderer" || true

if [[ "${DRY_RUN}" == "true" ]]; then
  echo "[Slim] Dry run complete. Not pushing."
  exit 0
fi

git push -u origin "${TARGET_BRANCH}" --force-with-lease
echo "[Slim] Pushed ${TARGET_BRANCH}"
