# Krema AI: High-Signal Communication Protocol

> **Core Principle:** Efficiency is a side effect of precision. Ground every action in our established truth, not in broad assumptions. Token efficiency and accuracy are the highest priorities.

## 1. Context Grounding (The 'Truth-First' Rule)
- **Anchor Your Work:** Before executing any task, check `ARCHITECTURE Mandate.md` and the relevant Research Station registry (`docs/research/`). Use these as your primary context rather than dumping entire file contents.
- **Surgical Access:** Use `grep_search` to target specific lines. Do not read entire files unless the change is system-wide. Always use `start_line` and `end_line` bounds when reading files.
- **Reference, Don't Duplicate:** Refer to our existing mandates by rule number (e.g., "Per Rule 17, I am adjusting the unit variables...") rather than re-explaining the rationale.

## 2. Minimalist Interaction & Zero-Filler Reporting
- **Zero-Filler:** No conversational preambles ("I will now...", "Here is the code..."). Outputs must be strictly structured: **[Action Taken]**, **[Result/Bug Found]**, **[Next Step Request]**.
- **One-Turn Resolution:** Aim to combine search and planning into a single turn whenever possible. Execute independent searches, reads, and commands in parallel to minimize conversational back-and-forths.
- **Concise Reporting:** Use the `update_topic` tool to provide high-level summaries. Do not repeat state information already in `work-state.md` or the `Research Station`.

## 3. Surgical Accuracy & Bracket Awareness
- **Bracket & Block Awareness:** Be extremely aware of brackets and code blocks. Never delete or add extra brackets. Verify indentation and context before modifying any block.
- **Targeted Edits:** Only use the `replace` tool for targeted blocks (< 50 lines). Chunk complex refactors to avoid large diffs in the context window.
- **Mandate Check:** If a request conflicts with the `ARCHITECTURE Mandate.md`, identify the conflict immediately and pause execution. Do not implement a solution that breaks the constitution.
- **Registry Compliance:** If your change involves a new variable, layer, or module, confirm it is registered in `docs/research/` *before* the commit.

## 4. Operational Efficiency
- **Sub-Agent Delegation:** Use sub-agents for heavy lifting (large search/replace across many files) to keep this main conversation history lean.
- **State Reliance:** Rely entirely on `.claude/work-state.md` and `GEMINI.md` for memory. Do not re-explain known architectural rules.
- **Error Persistence:** If a tool call fails, do not explain the failure in detail—summarize the bottleneck and propose the alternative path.
