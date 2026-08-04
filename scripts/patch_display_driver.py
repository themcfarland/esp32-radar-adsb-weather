"""PlatformIO pre-build patch for the working Waveshare 7-inch driver.

The original v9 panel timing (16 MHz and all porch/polarity values) is kept
unchanged. Only the RGB bounce buffer is enlarged from 10 to 20 scanlines.
This is deliberately conservative: no runtime PCLK changes and no panel
restart calls are introduced.
"""
from pathlib import Path
import re

Import("env")  # type: ignore[name-defined]


def _patch_bounce(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    pattern = (
        r"(#define\s+LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE\s*"
        r"\(\s*LVGL_PORT_DISP_WIDTH\s*\*\s*)(?:10|20)(\s*\))"
    )
    updated, count = re.subn(pattern, r"\g<1>20\g<2>", text, count=1,
                             flags=re.MULTILINE)
    if not count:
        return False
    if updated != text:
        path.write_text(updated, encoding="utf-8")
        print(f"[display-patch] RGB bounce buffer = 20 lines: patched {path}")
    else:
        print("[display-patch] RGB bounce buffer = 20 lines: already configured")
    return True


def patch_display_driver(*_args, strict=False, **_kwargs):
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    if not libdeps.exists():
        if strict:
            raise RuntimeError(f"PlatformIO dependencies are missing: {libdeps}")
        print(f"[display-patch] waiting for dependencies in {libdeps}")
        return

    patched = False
    for header in libdeps.rglob("Waveshare_ST7262_LVGL.h"):
        patched |= _patch_bounce(header)

    if not patched:
        message = "Display driver patch failed: bounce-buffer macro not found"
        if strict:
            raise RuntimeError(message)
        print("[display-patch] waiting: " + message)



# PlatformIO installs lib_deps before loading a pre: extra script. Patch the
# driver immediately while the SCons environment is being prepared. Do not
# register a buildprog action: recent SCons versions call actions with keyword
# arguments and, more importantly, buildprog may run only after firmware.bin
# has already been generated.
patch_display_driver(strict=True)
