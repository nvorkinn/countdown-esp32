"""Build a standalone image using the same images and offsets as PlatformIO's uploader."""

import subprocess

Import("env")


def merge_firmware(source, target, env):
    images = [
        (offset, env.subst(path))
        for offset, path in env["FLASH_EXTRA_IMAGES"]
    ]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(source[-1])))
    # esptool is a Python script (esptool.py) up to platform 7.x, and a native program from 8.0, whose commands and
    # options are spelt with hyphens (the underscore spellings are only deprecated aliases there).
    esptool = env.subst("$OBJCOPY")
    native = not esptool.endswith(".py")
    dash = "-" if native else "_"
    args = [] if native else [env.subst("$PYTHONEXE")]
    args += [
        esptool,
        "--chip", env.BoardConfig().get("build.mcu"),
        f"merge{dash}bin",
        "--output", str(target[0]),
        f"--flash{dash}mode", env.subst("${__get_board_flash_mode(__env__)}"),
        f"--flash{dash}freq", env.subst("${__get_board_f_image(__env__)}"),
        f"--flash{dash}size", env.BoardConfig().get("upload.flash_size"),
    ]
    for offset, path in sorted(images, key=lambda image: int(image[0], 0)):
        args.extend([offset, path])
    subprocess.run(args, check=True)


inputs = [env.subst(path) for _, path in env["FLASH_EXTRA_IMAGES"]]
inputs.append(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
merged = env.Command(
    "$BUILD_DIR/firmware-merged.bin",
    inputs,
    env.VerboseAction(merge_firmware, "Merging standalone flash image $TARGET"),
)
env.AlwaysBuild(merged)
env.Alias("buildprog", merged)
