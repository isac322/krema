Build and visually verify in KWin session:
1. Build: cmake --build build/dev --parallel
2. If build fails: fix and retry
3. Start KWin MCP session: mcp__kwin-mcp__session_start with app_command="./build/dev/bin/krema"
4. Wait 2 seconds, take screenshot
5. Check screenshot for visual correctness
6. Stop session
