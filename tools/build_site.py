"""Assemble the install page in site/: the page, the firmware images, and ESP Web Tools' manifest.

    python tools/build_site.py [--images DIR]

The images come from release/ (a local build) or DIR (a downloaded release). The manifest's version is the
firmware version in src/app.h, and every chip in CHIPS must have an image, so a page never offers a chip
it cannot flash.
"""
import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
CHIPS = {
    "esp32": "ESP32",
    "esp32s3": "ESP32-S3",
    "esp32c3": "ESP32-C3",
    "esp32c5": "ESP32-C5",
    "esp32c6": "ESP32-C6",
    "esp32h2": "ESP32-H2",
}


def firmware_version():
    with open(os.path.join(ROOT, "src", "app.h"), encoding="utf-8") as f:
        m = re.search(r"FW_MAJOR = (\d+), FW_MINOR = (\d+), FW_PATCH = (\d+)", f.read())
    return ".".join(m.groups())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--images", default=os.path.join(ROOT, "release"))
    ap.add_argument("--out", default=os.path.join(ROOT, "site"))
    a = ap.parse_args()

    if os.path.isdir(a.out):
        shutil.rmtree(a.out)
    shutil.copytree(os.path.join(ROOT, "installer"), a.out)

    version = firmware_version()
    builds = []
    for chip, family in CHIPS.items():
        name = f"cheerlights-esp32-{chip}.bin"
        src = os.path.join(a.images, name)
        if not os.path.exists(src):
            sys.exit(f"missing image: {src}")
        os.makedirs(os.path.join(a.out, "firmware"), exist_ok=True)
        shutil.copy(src, os.path.join(a.out, "firmware", name))
        builds.append({"chipFamily": family, "parts": [{"path": f"firmware/{name}", "offset": 0}]})

    manifest = {
        "name": "CheerLights firmware",
        "version": version,
        "home_assistant_domain": None,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": builds,
    }
    manifest = {k: v for k, v in manifest.items() if v is not None}
    with open(os.path.join(a.out, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    with open(os.path.join(a.out, "version.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(version + "\n")
    print(f"site/ ready: firmware {version}, {len(builds)} chips")


if __name__ == "__main__":
    main()
