# SPDX-License-Identifier: GPL-3.0-only
import os
import subprocess

Import("env")


def _resolve_esptool_cmd(target_env) -> list:
    objcopy = target_env.subst("$OBJCOPY")
    if objcopy.endswith(".py"):
        return [target_env.subst("$PYTHONEXE"), objcopy]
    if os.name == "nt" and not os.path.exists(objcopy) and os.path.exists(objcopy + ".exe"):
        return [objcopy + ".exe"]
    return [objcopy]


def merge_bin_action(source, target, env):
    mcu = env.BoardConfig().get("build.mcu", "esp32")
    flash_mode = env.subst("${__get_board_flash_mode(__env__)}")
    flash_freq = env.subst("${__get_board_f_image(__env__)}")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    app_offset = env.subst("$ESP32_APP_OFFSET")
    firmware_bin = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    merged_bin = env.subst("$BUILD_DIR/firmware-merged.bin")

    images = [
        (str(offset), env.subst(path))
        for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    ]
    images.append((app_offset, firmware_bin))

    for offset, img_path in images:
        if not os.path.isfile(img_path):
            raise RuntimeError(
                f"Missing flash image for merge-bin at offset {offset}: {img_path}"
            )

    cmd = _resolve_esptool_cmd(env) + [
        "--chip",
        mcu,
        "merge-bin",
        "-o",
        merged_bin,
        "--flash-mode",
        flash_mode,
        "--flash-freq",
        flash_freq,
        "--flash-size",
        flash_size,
    ]
    for offset, img_path in images:
        cmd.extend([offset, img_path])

    print(f"Merging flashable image for {mcu} -> {merged_bin}")
    subprocess.run(cmd, check=True)
    size_bytes = os.path.getsize(merged_bin)
    print(f"Merged binary created: {merged_bin} ({size_bytes} bytes)")


env.AddCustomTarget(
    name="mergebin",
    dependencies=["$BUILD_DIR/${PROGNAME}.bin"],
    actions=[merge_bin_action],
    title="Merge Flashable Binary",
    description="Merge bootloader, partition table, boot_app0, and firmware into firmware-merged.bin",
)
