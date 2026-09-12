#!/usr/bin/env python3
import sys
import subprocess
import re

def run_build(command):
    print(f"--- Running Filtered Build: {' '.join(command)} ---")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )

    error_patterns = [
        re.compile(r".*error:.*", re.IGNORECASE),
        re.compile(r".*fatal error:.*", re.IGNORECASE),
        re.compile(r".*undefined reference to.*", re.IGNORECASE),
        re.compile(r".*static_assertion failed.*", re.IGNORECASE),
        re.compile(r".*FAILED.*"),
        re.compile(r".*segmentation fault.*", re.IGNORECASE)
    ]

    buffer = []
    found_errors = []
    
    for line in process.stdout:
        buffer.append(line.strip())
        if len(buffer) > 5:
            buffer.pop(0)
            
        for pattern in error_patterns:
            if pattern.match(line):
                # Capture context (last 3 lines) + the error
                error_context = "\n".join(buffer[-4:])
                found_errors.append(error_context)
                break
        
        # Limit to first 5 unique-ish errors to prevent bloat
        if len(found_errors) >= 5:
            break

    process.wait()
    exit_code = process.returncode

    if exit_code != 0:
        print("\n--- BUILD FAILED ---")
        if not found_errors:
            print("CRITICAL: No known error patterns matched. Dumping last 20 lines:")
            for line in buffer:
                print(line)
        else:
            for i, err in enumerate(found_errors):
                print(f"\n[Error {i+1}]\n{err}")
    else:
        print("\n--- BUILD SUCCESS ---")
    
    sys.exit(exit_code)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: build_filter.py <command>")
        sys.exit(1)
    run_build(sys.argv[1:])
