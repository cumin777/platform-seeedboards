# Default flags for bare-metal programming (without any framework layers)

from SCons.Script import Import

Import("env")
env.Append(
    ASFLAGS=["-mthumb"],
    ASPPFLAGS=["-x", "assembler-with-cpp"],
    CCFLAGS=[
        "-Os", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-mthumb", "-nostdlib"
    ],
    CXXFLAGS=["-fno-rtti", "-fno-exceptions"],
    CPPDEFINES=[("F_CPU", "$BOARD_F_CPU")],
    LINKFLAGS=[
        "-Os", "-Wl,--gc-sections,--relax", "-mthumb",
        "--specs=nano.specs", "--specs=nosys.specs"
    ],
    LIBS=["c", "gcc", "m", "stdc++", "nosys"]
)

if "BOARD" in env:
    env.Append(
        ASFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")],
        CCFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")],
        LINKFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")]
    )
