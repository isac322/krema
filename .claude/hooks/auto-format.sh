#!/bin/bash
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

case "$FILE_PATH" in
  *.cpp|*.h|*.hpp)
    clang-format -i "$FILE_PATH"
    ;;
esac
exit 0
