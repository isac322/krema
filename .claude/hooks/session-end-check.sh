#!/bin/bash
# Stop hook: remind Claude to update work-state.md before session ends

INPUT=$(cat)

cd "$(git rev-parse --show-toplevel)" 2>/dev/null || exit 0

# Check if src/ files were modified (staged or unstaged)
CHANGED=$(git diff --name-only HEAD 2>/dev/null | grep "^src/" | head -5)

if [ -n "$CHANGED" ]; then
  echo "=== SESSION END REMINDER ==="
  echo "src/ 파일이 변경되었습니다. 세션 종료 전 반드시:"
  echo "1. .claude/work-state.md를 현재 작업 상태로 갱신 (진행 중인 작업, 알려진 이슈, 다음 작업)"
  echo "2. ROADMAP.md 체크박스 갱신 (해당 시)"
  echo "변경된 파일: $CHANGED"
  echo "==========================="
fi

# advisory only — do not block session end
exit 0
