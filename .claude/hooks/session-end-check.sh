#!/bin/bash
# Stop hook: remind Claude to update work-state.md and check e2e scenarios before session ends

INPUT=$(cat)

cd "$(git rev-parse --show-toplevel)" 2>/dev/null || exit 0

# Check if src/ files were modified (staged or unstaged)
CHANGED=$(git diff --name-only HEAD 2>/dev/null | grep "^src/" | head -20)

if [ -n "$CHANGED" ]; then
  echo "=== SESSION END REMINDER ==="
  echo "src/ 파일이 변경되었습니다. 세션 종료 전 반드시:"
  echo "1. .claude/work-state.md를 현재 작업 상태로 갱신 (진행 중인 작업, 알려진 이슈, 다음 작업)"
  echo "2. ROADMAP.md 체크박스 갱신 (해당 시)"
  echo ""

  # Check which e2e scenarios are affected by the changed files
  SCENARIO_DIR="tests/e2e/scenarios"
  if [ -d "$SCENARIO_DIR" ]; then
    AFFECTED_SCENARIOS=""
    for FILE in $CHANGED; do
      # Search for this file path in scenario Affected Files sections
      MATCHES=$(grep -rl "$FILE" "$SCENARIO_DIR"/*.md 2>/dev/null | sort -u)
      if [ -n "$MATCHES" ]; then
        for SCENARIO in $MATCHES; do
          SCENARIO_NAME=$(basename "$SCENARIO")
          if ! echo "$AFFECTED_SCENARIOS" | grep -q "$SCENARIO_NAME"; then
            AFFECTED_SCENARIOS="$AFFECTED_SCENARIOS $SCENARIO_NAME"
          fi
        done
      fi
    done

    if [ -n "$AFFECTED_SCENARIOS" ]; then
      echo "3. E2E 시나리오 영향 확인 필요:"
      for S in $AFFECTED_SCENARIOS; do
        echo "   - tests/e2e/scenarios/$S"
      done
      echo "   → 기능 동작 변경 시 시나리오 스텝/검증 기준도 갱신할 것"
    fi
  fi

  echo ""
  echo "변경된 파일: $CHANGED"
  echo "==========================="
fi

# advisory only — do not block session end
exit 0
