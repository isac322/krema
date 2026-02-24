Commit current changes with Conventional Commits format:

1. Run arch-auditor agent to check for anti-pattern violations
2. If violations found: show them and stop (do not commit)
3. If clean:
   a. Run `git status` and `git diff --cached` (or `git diff HEAD` if nothing staged)
   b. Draft a Conventional Commit message in English (feat:/fix:/refactor:/docs: etc.)
   c. Show the message to the user for approval
   d. After approval: `git add` relevant files, `git commit`
   e. Run `git clang-format HEAD~1` to verify formatting
