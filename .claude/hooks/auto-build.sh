#!/bin/bash
# PostToolUse hook: auto-build after C++/CMake/QML file edits

INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

# Only trigger for C++, CMake, and QML files
case "$FILE_PATH" in
  *.cpp|*.h|*.hpp|*.qml|*/CMakeLists.txt|*.cmake)
    ;;
  *)
    exit 0  # Ignore other files
    ;;
esac

cd /home/bhyoo/projects/c++/krema

# Build (merge stderr into stdout)
BUILD_OUTPUT=$(cmake --build build/dev --parallel 2>&1)
BUILD_EXIT=$?

if [ $BUILD_EXIT -ne 0 ]; then
  # Build failed: extract and show errors
  echo "BUILD FAILED after editing $FILE_PATH"
  echo "$BUILD_OUTPUT" | grep -E "error:|fatal error:" | head -20
  echo "---"
  echo "Fix the build errors before continuing."
  exit 2  # exit 2 = block (force Claude to fix)
fi

# Build succeeded: pass silently
exit 0
