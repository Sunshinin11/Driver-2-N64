import os
import subprocess
import time
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if os.path.exists("/mnt/c/"):
    ares_exe = "/mnt/c/Users/georg/Downloads/ares-v148/ares.exe"
    rom = "C:\\Desenvolvimento\\Nintendo 64\\Driver 2 64\\D2_N64\\d2_n64.z64"
else:
    ares_exe = "C:/Users/georg/Downloads/ares-v148/ares.exe"
    rom = os.path.join(root, "d2_n64.z64")

if len(sys.argv) > 3:
    rom = os.path.join(os.path.dirname(rom), sys.argv[3]) if "\\" not in rom else rom.rsplit("\\", 1)[0] + "\\" + sys.argv[3]
print("Starting Ares with ROM:", rom)
out_log = sys.argv[1] if len(sys.argv) > 1 else "test_bench_stdout.log"
timeout_s = int(sys.argv[2]) if len(sys.argv) > 2 else 100
out_path = os.path.join(root, out_log) if not os.path.isabs(out_log) else out_log
err_path = os.path.join(root, "test_bench_stderr.log")
with open(out_path, "w", encoding="utf-8") as out, open(err_path, "w", encoding="utf-8") as err:
    proc = subprocess.Popen([ares_exe, rom], stdout=out, stderr=err)
    try:
        time.sleep(timeout_s)
    finally:
        proc.kill()
print("Ares finished. Log written to", out_path)
