# Build the vendored XCPlite subset in xcplite/ into a static library and put
# its headers on the include path.
#
# Keep XCPLITE_SOURCES in sync with the .c files copied by
# tools/update_xcplite.sh.

from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
xcplite_inc = project_dir / "xcplite" / "inc"
xcplite_src = project_dir / "xcplite" / "src"

if not xcplite_src.is_dir():
    raise RuntimeError(f"Vendored XCPlite sources not found at {xcplite_src}. Run tools/update_xcplite.sh")

env.Append(CPPPATH=[str(xcplite_inc), str(xcplite_src)])

# The subset of XCPlite needed for a 32-bit FreeRTOS target in the rtos
# configuration. The A2L generator, persistence, shared memory and the 64-bit
# queue variants are disabled by xcplib_rtos_cfg.h and are not built.
XCPLITE_SOURCES = [
    "cal.c",
    "platform.c",
    "queue32m.c",
    "xcpappl.c",
    "xcpethserver.c",
    "xcpethtl.c",
    "xcplite.c",
]

env.BuildSources(
    "$BUILD_DIR/xcplite",
    str(xcplite_src),
    src_filter=["+<{}>".format(source) for source in XCPLITE_SOURCES],
)
