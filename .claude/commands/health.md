Analyze development health metrics for the Krema project:

1. **Churn Analysis** — Find files with most changes:
   - Run: `git log --format=format: --name-only --since="30 days ago" | sort | uniq -c | sort -rn | head -10`
   - Report the top 5 most-changed files (high churn = potential instability)

2. **Recent Activity** — Summarize recent development:
   - Run: `git log --oneline --since="7 days ago"` for last week's commits
   - Categorize by type (feat/fix/refactor/docs)

3. **Anti-Pattern Violation History** — Check arch-auditor memory:
   - Read `.claude/projects/-home-bhyoo-projects-c---krema/memory/` for any arch-auditor violation records
   - Report most frequent violation categories
   - Flag any files with recurring violations

4. **Test Coverage** — Check test status:
   - List existing test files in `tests/`
   - List C++ classes that lack corresponding tests
   - Run tests if they exist: `cmake --build build/dev --target test`

5. **Knowledge Base Status** — Check docs/kde/ completeness:
   - Count documented vs undocumented APIs used in source code
   - Flag any KDE API usage without corresponding docs/kde/ entry

6. **Output Report**:
   ```
   DEVELOPMENT HEALTH REPORT
   =========================
   Period: Last 30 days

   Churn (Top 5):
     1. path/to/file — N changes
     ...

   Recent Activity (7 days):
     feat: N | fix: N | refactor: N | docs: N

   Anti-Pattern Violations:
     Most common: {category}
     Hotspot files: {files}

   Test Coverage:
     Classes with tests: N / M total
     Missing: {list}

   Knowledge Base:
     Documented APIs: N
     Undocumented usage: {list}

   Recommendations:
     1. ...
     2. ...
   ```
