import os
import time

CONFIG_PATH = os.path.expanduser("~/.config/kremarc")

def monitor():
    print(f"--- KREMA GROUP MONITOR ---")
    print(f"Monitoring: {CONFIG_PATH}")
    
    last_mtime = 0
    if os.path.exists(CONFIG_PATH):
        last_mtime = os.path.getmtime(CONFIG_PATH)

    try:
        while True:
            if os.path.exists(CONFIG_PATH):
                current_mtime = os.path.getmtime(CONFIG_PATH)
                if current_mtime != last_mtime:
                    print(f"\n[CONFIG] File changed!")
                    current_group = ""
                    with open(CONFIG_PATH, 'r') as f:
                        for line in f:
                            line = line.strip()
                            if line.startswith("["):
                                current_group = line
                            if "MaxZoomFactor" in line:
                                print(f"  {current_group} > {line}")
                    last_mtime = current_mtime
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nStopping monitor.")

if __name__ == "__main__":
    monitor()
