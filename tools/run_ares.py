import os
import subprocess
import time
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rom = os.path.join(root, "d2_n64.z64")
ares_exe = "C:/Users/georg/Downloads/ares-v148/ares.exe"

print("Starting Ares with ROM:", rom)
out_path = os.path.join(root, "test_bench_stdout.log")
err_path = os.path.join(root, "test_bench_stderr.log")
with open(out_path, "w", encoding="utf-8") as out, open(err_path, "w", encoding="utf-8") as err:
    proc = subprocess.Popen([ares_exe, rom], stdout=out, stderr=err)
    try:
        time.sleep(85)
    finally:
        proc.kill()
print("Ares finished. Log written to", out_path)
