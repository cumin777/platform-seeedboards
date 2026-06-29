import sys
import os
import time
from os.path import isdir, join

from SCons.Script import (ARGUMENTS, COMMAND_LINE_TARGETS, AlwaysBuild,
                          Builder, Default, DefaultEnvironment)

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

env.Replace(
    AR="arm-none-eabi-ar",
    AS="arm-none-eabi-as",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-ranlib",
    SIZETOOL="arm-none-eabi-size",

    ARFLAGS=["rc"],

    SIZEPROGREGEXP=r"^(?:\.text|\.data|\.rodata|\.text.align|\.ARM.exidx)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.noinit)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD='$SIZETOOL -B -d $SOURCES',

    PROGSUFFIX=".elf"
)

# Allow user to override via pre:script
if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

env.Append(
    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(" ".join([
                "$OBJCOPY", "-O", "binary", "$SOURCES", "$TARGET"
            ]), "Building $TARGET"),
            suffix=".bin"
        ),
        ElfToHex=Builder(
            action=env.VerboseAction(" ".join([
                "$OBJCOPY", "-O", "ihex", "-R", ".eeprom",
                "$SOURCES", "$TARGET"
            ]), "Building $TARGET"),
            suffix=".hex"
        ),
        BinToUf2=Builder(
            action=env.VerboseAction(" ".join([
                '"$PYTHONEXE"',
                '"%s"' % join(platform.get_dir(), "builder", "tools", "uf2conv.py"),
                "-i", "$SOURCES",
                "-b", "${UF2_BASE_ADDR}",
                "--family-id", "${UF2_FAMILY_ID}",
                "-o", "$TARGET"
            ]), "Building $TARGET"),
            suffix=".uf2"
        )
    )
)

upload_protocol = env.subst("$UPLOAD_PROTOCOL")

if not env.get("PIOFRAMEWORK"):
    env.SConscript("frameworks/_bare.py")

#
# Target: Build executable and linkable firmware
#

if "zephyr" in env.get("PIOFRAMEWORK", []):
    env.SConscript(
        join(platform.get_package_dir(
            "framework-zephyr"), "scripts", "platformio", "platformio-build-pre.py"),
        exports={"env": env}
    )

target_elf = None
if "nobuild" in COMMAND_LINE_TARGETS:
    target_elf = join("$BUILD_DIR", "${PROGNAME}.elf")
    target_firm = join("$BUILD_DIR", "${PROGNAME}.hex")
else:
    target_elf = env.BuildProgram()
    target_firm = env.ElfToHex(
        join("$BUILD_DIR", "${PROGNAME}"), target_elf)
    env.Depends(target_firm, "checkprogsize")

AlwaysBuild(env.Alias("nobuild", target_firm))
target_buildprog_sources = [target_firm]

#
# Target: Print binary size
#
target_size = env.AddPlatformTarget(
    "size",
    target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"),
    "Program Size",
    "Calculate program size",
)

#
# Target: Upload firmware
#
debug_tools = env.BoardConfig().get("debug.tools", {})
upload_actions = []
target_bin = None
target_uf2 = None
_uf2_cfg = board.get("upload.uf2", {})
_has_uf2_support = bool(_uf2_cfg) or "uf2" in board.get("upload.protocols", [])

if _has_uf2_support:
    env.Replace(
        UF2_BASE_ADDR=str(board.get("upload.offset_address", "0x08008000")),
        UF2_FAMILY_ID=str(_uf2_cfg.get("family_id", "0x00C5C5C5"))
    )
    target_bin = env.ElfToBin(
        join("$BUILD_DIR", "${PROGNAME}"), target_elf)
    target_uf2 = env.BinToUf2(
        join("$BUILD_DIR", "${PROGNAME}"), target_bin)
    env.AddPlatformTarget(
        "uf2",
        target_uf2,
        target_uf2,
        "Build UF2 Image",
        "Build UF2 image for UF2 bootloader upload"
    )
    target_buildprog_sources.append(target_uf2)

target_buildprog = env.Alias("buildprog", target_buildprog_sources, target_buildprog_sources)

if upload_protocol == "uf2":
    # Upload the prebuilt .uf2 artifact via Python script
    _tools_dir = join(platform.get_dir(), "builder", "tools")
    _uf2_volume_label = _uf2_cfg.get("volume_label", "XIAOC5BOOT")
    _upload_cmd = ' '.join([
        '"$PYTHONEXE"',
        '"%s"' % join(_tools_dir, "uf2upload.py"),
        '"$SOURCE"',
        '--label', str(_uf2_volume_label),
        '--port', '"${UPLOAD_PORT}"',
    ])

    env.Replace(
        UPLOADCMD=_upload_cmd
    )

    def _uf2_before_upload(target, source, env):  # pylint: disable=W0613,W0621
        env.AutodetectUploadPort()
        upload_options = board.get("upload", {})
        if bool(upload_options.get("use_1200bps_touch", False)):
            env.TouchSerialPort("$UPLOAD_PORT", 1200)
            # Give the board time to reboot into TinyUF2 bootloader mode
            time.sleep(0.5)

    upload_actions = [
        env.VerboseAction(
            _uf2_before_upload,
            "Triggering bootloader via 1200-bps touch"
        ),
        env.VerboseAction(
            "$UPLOADCMD",
            "Uploading via UF2"
        )
    ]
    env.AddPlatformTarget("upload", target_uf2, upload_actions, "Upload")

elif upload_protocol == "stlink":
    # Use STM32CubeProgrammer CLI for STLink upload
    stm32prog = "STM32_Programmer_CLI"
    upload_offset = board.get("upload.offset_address", "0x08000000")
    env.Replace(
        UPLOADER=stm32prog,
        UPLOADERFLAGS=[
            "-c", "port=swd",
            "-w", "$SOURCE", str(upload_offset),
            "-v",
            "-rst"
        ],
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS"
    )
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]
    env.AddPlatformTarget("upload", target_firm, upload_actions, "Upload")

elif upload_protocol in debug_tools:
    # Fallback: use OpenOCD if configured
    openocd_args = [
        "-d%d" % (2 if int(ARGUMENTS.get("PIOVERBOSE", 0)) else 1)
    ]
    openocd_args.extend(
        debug_tools.get(upload_protocol).get("server").get("arguments", []))
    if env.GetProjectOption("debug_speed"):
        openocd_args.extend(
            ["-c", "adapter speed %s" % env.GetProjectOption("debug_speed")]
        )
    openocd_args.extend([
        "-c", "init; reset halt; program {$SOURCE} verify reset exit; shutdown"
    ])
    openocd_args = [
        f.replace("$PACKAGE_DIR",
                  platform.get_package_dir("tool-openocd") or "")
        for f in openocd_args
    ]
    env.Replace(
        UPLOADER="openocd",
        UPLOADERFLAGS=openocd_args,
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS")
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]
    env.AddPlatformTarget("upload", target_firm, upload_actions, "Upload")

elif upload_protocol == "custom":
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]
    env.AddPlatformTarget("upload", target_firm, upload_actions, "Upload")

else:
    sys.stderr.write("Warning! Unknown upload protocol %s\n" % upload_protocol)

#
# Default targets
#
Default([target_buildprog, target_size])
