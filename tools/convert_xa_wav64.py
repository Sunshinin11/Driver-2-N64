"""Convert interleaved PS1 XA speech to individual cartridge WAV64 streams."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

SECTOR_SIZE = 2352
SYNC = b"\0" + b"\xff" * 10 + b"\0"
BANK = re.compile(r"DRIVER2/XA/XABNK0([1-4])\.XA$", re.IGNORECASE)


def convert_xa(disc, sector_size, files, output):
    banks = [(BANK.fullmatch(path), lba, size) for path, lba, size in files]
    if sector_size != SECTOR_SIZE or not any(match for match, _, _ in banks):
        raise ValueError("XA requires the raw CD1 image with 2352-byte sectors")
    output = Path(output) / "XA64"
    output.mkdir(parents=True, exist_ok=True)
    audioconv = os.environ.get("AUDIOCONV64", "/opt/libdragon/bin/audioconv64")
    for match, lba, size in banks:
        if not match:
            continue
        bank = int(match[1]) - 1
        tracks = {}
        disc.seek(lba * SECTOR_SIZE)
        for _ in range((size + 2047) // 2048):
            sector = disc.read(SECTOR_SIZE)
            if len(sector) != SECTOR_SIZE or sector[:12] != SYNC:
                raise ValueError(f"truncated or invalid raw sector in XA bank {bank}")
            file_id, channel, flags, _ = sector[16:20]
            if sector[15] == 2 and file_id == 1 and flags & 4:
                tracks.setdefault(channel, bytearray()).extend(sector)
        if not tracks:
            raise ValueError(f"no audio sectors found in XA bank {bank}")
        with tempfile.TemporaryDirectory() as tmp:
            for channel, data in sorted(tracks.items()):
                name = f"B{bank}_T{channel:02}"
                raw = Path(tmp) / f"{name}.str"
                wav = raw.with_suffix(".wav")
                raw.write_bytes(data)
                subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-xerror",
                                "-f", "psxstr", "-i", str(raw), "-map", "0:a:0",
                                "-ac", "1", "-ar", "22050", str(wav)], check=True)
                subprocess.run([audioconv, "--wav-compress", "1", "-o", str(output), str(wav)],
                               check=True)
