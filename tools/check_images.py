"""Check each image in release/: the partition table inside it is this repo's, and the image ends before the settings.

    python tools/check_images.py

Exits 1 when an image is missing, has another partition table, or reaches into the settings store,
which would erase a board's settings on an update.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CHIPS = ["esp32", "esp32s3", "esp32c3", "esp32c5", "esp32c6", "esp32h2"]
TABLE_OFFSET = 0x8000


def expected():
    rows = []
    with open(os.path.join(ROOT, "partitions.csv"), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                name, _, _, offset, size = [x.strip() for x in line.split(",")[:5]]
                rows.append((name, int(offset, 16), int(size, 16)))
    return rows


def table(image):
    rows = []
    for at in range(TABLE_OFFSET, TABLE_OFFSET + 0xC00, 32):
        entry = image[at:at + 32]
        if entry[:2] != b"\xaa\x50":
            break
        offset, size = struct.unpack("<II", entry[4:12])
        rows.append((entry[12:28].rstrip(b"\x00").decode(), offset, size))
    return rows


def main():
    want = expected()
    nvs = next(offset for name, offset, _ in want if name == "nvs")
    failed = 0
    for chip in CHIPS:
        path = os.path.join(ROOT, "release", f"cheerlights-esp32-ble-{chip}.bin")
        if not os.path.exists(path):
            print(f"FAIL {chip}: no image")
            failed += 1
            continue
        with open(path, "rb") as f:
            image = f.read()
        got = table(image)
        ok = got == want and len(image) <= nvs
        failed += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'} {chip}: {len(image)} bytes, partitions {[n for n, _, _ in got]}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
