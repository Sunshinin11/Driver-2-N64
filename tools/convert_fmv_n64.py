"""Convert selected STR videos to MPEG-1 and synchronized WAV64 streams."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

MOVIE = re.compile(r"DRIVER2/FMV/\d+/RENDER(\d+)\.STR$", re.IGNORECASE)
SECTOR_SIZE = 2352


def convert_fmv(disc, sector_size, files, output, selection):
    if sector_size != SECTOR_SIZE:
        raise ValueError("FMV requires a raw disc image with 2352-byte sectors")
    output = Path(output) / "FMV64"
    output.mkdir(parents=True, exist_ok=True)
    audioconv = os.environ.get("AUDIOCONV64", "/opt/libdragon/bin/audioconv64")
    found = set()
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "movie.str"
        for path, lba, size in files:
            match = MOVIE.fullmatch(path)
            if not match:
                continue
            number = int(match[1])
            if selection is not None and number not in selection:
                continue
            stem = f"R{number:02}"
            video = output / f"{stem}.m1v"
            if video.exists():
                found.add(number)
                continue
            disc.seek(lba * SECTOR_SIZE)
            remaining = ((size + 2047) // 2048) * SECTOR_SIZE
            with raw.open("wb") as dest:
                while remaining:
                    data = disc.read(min(remaining, 1 << 20))
                    if not data:
                        raise ValueError(f"truncated video {path}")
                    dest.write(data)
                    remaining -= len(data)

            ffmpeg = ["ffmpeg", "-y", "-nostdin", "-loglevel", "error",
                      "-f", "psxstr", "-i", str(raw)]
            encoded = Path(tmp) / f"{stem}.m1v"
            subprocess.run(ffmpeg + [
                "-map", "0:v:0", "-vf", "scale=192:144,setsar=1,fps=24",
                "-c:v", "mpeg1video", "-pix_fmt", "yuv420p", "-bf", "0",
                "-b:v", "100k", "-f", "mpeg1video", str(encoded)], check=True)

            probe = subprocess.run([
                "ffprobe", "-v", "error", "-f", "psxstr", "-select_streams", "a:0",
                "-show_entries", "stream=index", "-of", "csv=p=0", str(raw)],
                check=True, capture_output=True)
            if probe.stdout.strip():
                audio = Path(tmp) / f"{stem}.wav"
                subprocess.run(ffmpeg + ["-map", "0:a:0", "-ac", "1", "-ar",
                                         "16000", str(audio)], check=True)
                subprocess.run([audioconv, "--wav-compress", "1", "-o", str(output), str(audio)],
                               check=True)
            video.write_bytes(encoded.read_bytes())
            found.add(number)
    return found
