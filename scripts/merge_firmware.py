"""Build a standalone image using the same images and offsets as PlatformIO's uploader."""

import subprocess

Import("env")


def merge_firmware(source, target, env):
    images = [
        (offset, env.subst(path))
        for offset, path in env["FLASH_EXTRA_IMAGES"]
    ]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(source[-1])))
    args = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),
        "--chip", env.BoardConfig().get("build.mcu"),
        "merge_bin",
        "--output", str(target[0]),
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", env.BoardConfig().get("upload.flash_size"),
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
