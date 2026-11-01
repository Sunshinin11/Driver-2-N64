import os
import shutil

import os
import sys
import shutil

if sys.platform == "win32":
    ROOT_DIR = r"C:\Desenvolvimento\Nintendo 64\Driver 2 64"
else:
    ROOT_DIR = "/mnt/c/Desenvolvimento/Nintendo 64/Driver 2 64"

D2_DIR = os.path.join(ROOT_DIR, "D2_N64")
KEEP_ROM = os.path.normpath(os.path.join(D2_DIR, "d2_n64.z64"))

total_bytes_freed = 0
deleted_categories = {}

def record_deleted(category, path, size):
    global total_bytes_freed
    total_bytes_freed += size
    if category not in deleted_categories:
        deleted_categories[category] = {"count": 0, "bytes": 0}
    deleted_categories[category]["count"] += 1
    deleted_categories[category]["bytes"] += size

def delete_file(category, path):
    if os.path.exists(path) and os.path.isfile(path):
        size = os.path.getsize(path)
        try:
            os.remove(path)
            record_deleted(category, path, size)
        except Exception as e:
            print(f"Error removing file {path}: {e}")

def delete_dir(category, path):
    if os.path.exists(path) and os.path.isdir(path):
        size = 0
        for dirpath, dirnames, filenames in os.walk(path):
            for f in filenames:
                fp = os.path.join(dirpath, f)
                try:
                    size += os.path.getsize(fp)
                except Exception:
                    pass
        try:
            shutil.rmtree(path)
            record_deleted(category, path, size)
        except Exception as e:
            print(f"Error removing dir {path}: {e}")

# 1. Obsolete ROMs (*.z64)
print("--- Deleting obsolete ROMs ---")
for search_dir in [ROOT_DIR, D2_DIR]:
    for entry in os.listdir(search_dir):
        if entry.lower().endswith(".z64"):
            full_path = os.path.normpath(os.path.join(search_dir, entry))
            if full_path.lower() != KEEP_ROM.lower():
                delete_file("ROMs antigas (*.z64)", full_path)

# 2. Obsolete build directories in D2_N64
print("--- Deleting obsolete build directories ---")
for b_name in ["build_leg_clean", "build_leg_prof", "build_leg_ref", "build_t3d_prof", "build_t3d_ref"]:
    b_path = os.path.join(D2_DIR, b_name)
    delete_dir("Diretórios de build obsoletos", b_path)

# 3. Obsolete CI8 sprites in filesystem_vadpcm/TEX and build_d2/fs/TEX
print("--- Deleting obsolete CI8 sprites ---")
tex_dirs = [
    os.path.join(D2_DIR, "filesystem_vadpcm", "TEX"),
    os.path.join(D2_DIR, "build_d2", "fs", "TEX")
]
for td in tex_dirs:
    if os.path.exists(td):
        for entry in os.listdir(td):
            if entry.lower().endswith(".sprite") and entry.lower() != "bld234_ci4.sprite":
                delete_file("Sprites CI8 obsoletos", os.path.join(td, entry))

# 4. Old logs and reports in D2_N64 and root
print("--- Deleting old logs and reports ---")
for search_dir in [ROOT_DIR, D2_DIR]:
    for entry in os.listdir(search_dir):
        if entry.lower().endswith(".log") or (entry.lower().startswith("log_") and entry.lower().endswith(".txt")):
            delete_file("Logs e relatórios antigos", os.path.join(search_dir, entry))

logs_dir = os.path.join(D2_DIR, "logs")
if os.path.exists(logs_dir):
    for entry in os.listdir(logs_dir):
        if entry.lower().endswith(".log") or entry.lower().endswith(".txt"):
            delete_file("Logs e relatórios antigos", os.path.join(logs_dir, entry))

# 5. Temporary sync tarballs in _sync
print("--- Deleting temporary sync archives ---")
sync_dir = os.path.join(ROOT_DIR, "_sync")
if os.path.exists(sync_dir):
    delete_dir("Arquivos temporários de sincronização (_sync)", sync_dir)

print("\n================ CLEANUP SUMMARY ================")
for cat, data in deleted_categories.items():
    mb = data['bytes'] / (1024 * 1024)
    print(f"- {cat}: {data['count']} itens, {mb:.2f} MB ({data['bytes']} bytes)")

total_mb = total_bytes_freed / (1024 * 1024)
total_gb = total_bytes_freed / (1024 * 1024 * 1024)
print(f"\nTOTAL LIBERADO: {total_mb:.2f} MB ({total_gb:.2f} GB / {total_bytes_freed} bytes)")
print("=================================================")
