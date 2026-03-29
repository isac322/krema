#!/usr/bin/env bash
# PostToolUse hook: detect relevant code/doc changes and prompt docs-seo evaluation.
#
# Runs after Bash tool calls containing git commit/add.
# When changed files match the trigger patterns from .claude/agents/docs-seo.md,
# outputs a JSON context message instructing Claude Code to invoke the docs-seo agent.
#
# Exit 0 with no output = no-op (no relevant changes detected).

set -euo pipefail

INPUT="$(cat)"
TOOL="$(jq -r '.tool_name // empty' <<< "$INPUT")"

PROJECT_DIR="${CLAUDE_PROJECT_DIR:-$(pwd)}"

# Only inspect Bash calls with git commit/add
if [[ "$TOOL" != "Bash" ]]; then
  exit 0
fi

CMD="$(jq -r '.tool_input.command // empty' <<< "$INPUT")"
if ! echo "$CMD" | grep -qE '(git commit|git add)'; then
  exit 0
fi

# Collect changed files
FILES=()
while IFS= read -r f; do
  [[ -n "$f" ]] && FILES+=("$PROJECT_DIR/$f")
done < <(git -C "$PROJECT_DIR" diff --name-only HEAD 2>/dev/null || true)
while IFS= read -r f; do
  [[ -n "$f" ]] && FILES+=("$PROJECT_DIR/$f")
done < <(git -C "$PROJECT_DIR" diff --name-only --cached 2>/dev/null || true)

[[ ${#FILES[@]} -eq 0 ]] && exit 0

# Map changed files to trigger labels
TRIGGER_LABELS=()
MATCHED_FILES=()

for FILE in "${FILES[@]}"; do
  RELPATH="${FILE#"$PROJECT_DIR/"}"

  case "$RELPATH" in
    src/*.cpp|src/*.h)
      TRIGGER_LABELS+=("code-change")
      MATCHED_FILES+=("$RELPATH")
      ;;
    src/qml/*.qml)
      TRIGGER_LABELS+=("ui-change")
      MATCHED_FILES+=("$RELPATH")
      ;;
    src/config/krema.kcfg)
      TRIGGER_LABELS+=("settings-change")
      MATCHED_FILES+=("$RELPATH")
      ;;
    CMakeLists.txt)
      TRIGGER_LABELS+=("deps-change")
      MATCHED_FILES+=("$RELPATH")
      ;;
    src/com.bhyoo.krema.metainfo.xml)
      TRIGGER_LABELS+=("metainfo-update")
      MATCHED_FILES+=("$RELPATH")
      ;;
    CHANGELOG.md)
      TRIGGER_LABELS+=("changelog-update")
      MATCHED_FILES+=("$RELPATH")
      ;;
    README.md)
      TRIGGER_LABELS+=("readme-update")
      MATCHED_FILES+=("$RELPATH")
      ;;
    .claude/positioning.yml)
      TRIGGER_LABELS+=("manifest-update")
      MATCHED_FILES+=("$RELPATH")
      ;;
    marketing/strategy.md)
      TRIGGER_LABELS+=("strategy-update")
      MATCHED_FILES+=("$RELPATH")
      ;;
  esac
done

[[ ${#TRIGGER_LABELS[@]} -eq 0 ]] && exit 0

# Deduplicate
mapfile -t UNIQUE_LABELS < <(printf '%s\n' "${TRIGGER_LABELS[@]}" | sort -u)
mapfile -t UNIQUE_FILES  < <(printf '%s\n' "${MATCHED_FILES[@]}"  | sort -u)

LABELS_STR="$(IFS=', '; echo "${UNIQUE_LABELS[*]}")"
FILES_STR="$(IFS=', ';  echo "${UNIQUE_FILES[*]}")"

# Emit context message
jq -n \
  --arg labels "$LABELS_STR" \
  --arg files  "$FILES_STR" \
  '{
    hookSpecificOutput: {
      hookEventName: "PostToolUse",
      additionalContext: (
        "⚡ [docs-seo auto-trigger] Trigger labels: [" + $labels + "]\n" +
        "Changed files: " + $files + "\n\n" +
        "ACTION REQUIRED: Use the docs-seo agent (`.claude/agents/docs-seo.md`) to evaluate documentation drift.\n" +
        "Pass context: changed_files=[" + $files + "], trigger_labels=[" + $labels + "]\n" +
        "The agent must: (1) read `.claude/positioning.yml` first, (2) assemble the input set for each active trigger label, (3) detect gaps in documentation files, (4) update only the output targets for active labels, (5) report no-op if all conditions are met."
      )
    }
  }'
