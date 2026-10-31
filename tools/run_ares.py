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

print("Starting Ares with ROM:", rom)
out_path = os.path.join(root, "test_bench_stdout.log")
err_path = os.path.join(root, "test_bench_stderr.log")
with open(out_path, "w", encoding="utf-8") as out, open(err_path, "w", encoding="utf-8") as err:
    proc = subprocess.Popen([ares_exe, rom], stdout=out, stderr=err)
    try:
        time.sleep(170)
    finally:
        proc.kill()
print("Ares finished. Log written to", out_path)
