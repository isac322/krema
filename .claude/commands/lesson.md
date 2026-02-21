Record a bug-fix lesson in docs/kde/lessons-learned.md:

1. Ask the user for these 4 pieces of information (skip any they already provided):
   - **Symptom**: What was the observable problem?
   - **Root Cause**: What was actually wrong?
   - **Fix**: What was the solution?
   - **Lesson**: What general principle should we remember?

2. Read the current `docs/kde/lessons-learned.md` file

3. Append the new lesson in this format:
   ```
   ### {Short Title}
   - **Date**: YYYY-MM-DD
   - **Symptom**: {symptom}
   - **Root Cause**: {root cause}
   - **Fix**: {fix}
   - **Lesson**: {lesson}
   - **Files**: {affected files if known}
   ```

4. If the lesson reveals a pattern that should be an anti-pattern rule, suggest adding it to CLAUDE.md
