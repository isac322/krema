#!/usr/bin/env python3
import sys
import subprocess
import os

def trace_symbol(symbol, dir_path="src"):
    print(f"--- Tracing Symbol: {symbol} in {dir_path} ---")
    
    # Use ripgrep (rg) if available, fallback to grep
    try:
        cmd = ["rg", "-n", "-C", "2", symbol, dir_path]
        result = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        cmd = ["grep", "-rn", "-C", "2", symbol, dir_path]
        result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Symbol '{symbol}' not found.")
        return

    lines = result.stdout.splitlines()
    
    # High-Density Intelligence: Group by file
    files = {}
    for line in lines:
        if ":" in line:
            parts = line.split(":", 2)
            if len(parts) >= 2:
                filename = parts[0]
                content = parts[1:]
                if filename not in files:
                    files[filename] = []
                files[filename].append(":".join(content))

    for filename, matches in files.items():
        print(f"\nFile: {filename}")
        # Only show first 3 matches per file to keep it dense
        for match in matches[:3]:
            print(f"  {match}")
        if len(matches) > 3:
            print(f"  ... ({len(matches) - 3} more matches)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: trace_logic.py <symbol> [dir]")
        sys.exit(1)
    
    symbol = sys.argv[1]
    dir_path = sys.argv[2] if len(sys.argv) > 2 else "src"
    trace_symbol(symbol, dir_path)
