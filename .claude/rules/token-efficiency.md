# Token Efficiency Rules

## Read Only What You Need
- docs/kde/: Read ONLY the specific doc relevant to the current task, not all files
- Use `docs/kde/README.md` index to identify which doc to read
- For source files: use Grep to find relevant sections instead of reading entire files
- Do NOT re-read files that haven't changed since your last read

## Session Discipline
- After completing each logical unit of work (feature, bug fix, refactor): suggest running /compact
- If context is getting large, proactively summarize what's been done and what remains

## Subagent Efficiency
- Use model: "haiku" for ad-hoc Task agents doing simple searches, file reads, or pattern matching
- Named specialist agents (kde-researcher, product-quality, etc.) use their own configured model
- Reserve sonnet/opus for complex reasoning tasks
- Keep subagent prompts focused — avoid dumping entire context into subagents

## Compaction Priorities
- MUST preserve: .claude/work-state.md content, current task objective, test scenarios
- Safe to discard: file contents read but not modified, intermediate debugging steps, docs/kde/ content (re-readable), successful build output
