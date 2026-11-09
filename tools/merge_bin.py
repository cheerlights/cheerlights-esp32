"""After each ESP32 build, write release/cheerlights-esp32-ble-<chip>.bin: one file that flashes at offset 0x0.

It holds the bootloader, the partition table, and the app at the addresses the build uses,
so a browser flasher or `esptool write-flash 0x0` can install it without knowing them.
It is rebuilt whenever any of those parts changes, not only the app.
"""
import os
import subprocess

Import("env")  # noqa: F821  PlatformIO provides this when run as an extra script

board = env.BoardConfig()  # noqa: F821
chip = board.get("build.mcu")
out = os.path.join(env.subst("$PROJECT_DIR"), "release", f"cheerlights-esp32-ble-{chip}.bin")  # noqa: F821
extra = [(offset, env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]  # noqa: F821
app = env.subst("$BUILD_DIR/${PROGNAME}.bin")  # noqa: F821


def merge(target, source, env):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    images = []
    for offset, path in extra:
        images += [offset, path]
    images += [env.subst("$ESP32_APP_OFFSET"), app]
    cmd = [
        env.subst("$OBJCOPY"),
        "--chip", chip,
        "merge-bin",
        "-o", out,
        "--flash-mode", board.get("build.flash_mode", "dio"),
        "--flash-size", board.get("upload.flash_size", "4MB"),
    ] + images
    subprocess.run(cmd, check=True)
    print(f"Merged image: {out}")


merged = env.Command(out, [app] + [path for _, path in extra], merge)  # noqa: F821
env.Default(merged)  # noqa: F821
