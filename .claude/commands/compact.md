Compact the conversation while preserving critical Krema context:

When compacting, ALWAYS preserve:
- Current task objective and progress
- Files modified in this session and their current state
- Any build errors or bugs being debugged
- Architecture decisions from CLAUDE.md Anti-Patterns section
- .claude/work-state.md content (세션 간 상태 전달용 — 절대 삭제 금지)
- product-quality 테스트 시나리오 및 결과 (테스트 진행 중인 경우)

Summarize and discard:
- File contents that were read but not modified
- Intermediate debugging steps that led nowhere
- docs/kde/ content (can be re-read from files if needed)
- Build output from successful builds
