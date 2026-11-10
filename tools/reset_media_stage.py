"""Remove generated media so changing the ROM's selection cannot leave old clips."""
from pathlib import Path
import sys

stage = Path(sys.argv[1])
for directory, patterns in (("XA64", ("B?_T??.wav64",)), ("FMV64", ("R??.m1v", "R??.wav64"))):
    for pattern in patterns:
        for asset in (stage / directory).glob(pattern):
            asset.unlink()
