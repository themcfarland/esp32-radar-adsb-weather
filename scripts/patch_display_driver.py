"""PlatformIO pre-build patch for the Waveshare 7-inch RGB driver.

The panel timing remains unchanged. The script only sets the RGB bounce
buffer to 20 scanlines. It accepts any previously patched numeric value
(e.g. 10, 20 or 30), so switching between firmware versions does not require
manually deleting PlatformIO's .pio/libdeps cache.
"""
from pathlib import Path
import re

Import("env")  # type: ignore[name-defined]

BOUNCE_LINES = 20
MACRO_NAME = "LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE"

# Match the complete numeric multiplier while preserving whitespace and any
# trailing comment. Previous releases may have left 30 in .pio/libdeps.
_BOUNCE_PATTERN = re.compile(
    r"(^[ \t]*#define[ \t]+LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE[ \t]+"
    r"\([ \t]*LVGL_PORT_DISP_WIDTH[ \t]*\*[ \t]*)"
    r"\d+[uUlL]*"
    r"([ \t]*\)[^\r\n]*$)",
    flags=re.MULTILINE,
)


def _patch_bounce(path: Path) -> bool:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        print(f"[display-patch] cannot read {path}: {exc}")
        return False

    replacement = rf"\g<1>{BOUNCE_LINES}\g<2>"
    updated, count = _BOUNCE_PATTERN.subn(replacement, text, count=1)
    if not count:
        return False

    if updated != text:
        path.write_text(updated, encoding="utf-8")
        print(
            f"[display-patch] RGB bounce buffer = {BOUNCE_LINES} lines: "
            f"patched {path}"
        )
    else:
        print(
            f"[display-patch] RGB bounce buffer = {BOUNCE_LINES} lines: "
            "already configured"
        )
    return True


def _candidate_files(libdeps: Path):
    """Yield the known header first, then compatible fallback files."""
    seen = set()

    for path in libdeps.rglob("Waveshare_ST7262_LVGL.h"):
        resolved = path.resolve()
        if resolved not in seen:
            seen.add(resolved)
            yield path

    # Fallback for a future library layout/name change.
    for suffix in ("*.h", "*.hpp", "*.cpp"):
        for path in libdeps.rglob(suffix):
            resolved = path.resolve()
            if resolved in seen:
                continue
            try:
                if MACRO_NAME not in path.read_text(
                    encoding="utf-8", errors="ignore"
                ):
                    continue
            except OSError:
                continue
            seen.add(resolved)
            yield path


def patch_display_driver(*_args, strict=False, **_kwargs):
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    if not libdeps.exists():
        message = f"PlatformIO dependencies are missing: {libdeps}"
        if strict:
            raise RuntimeError(message)
        print("[display-patch] warning: " + message)
        return False

    for candidate in _candidate_files(libdeps):
        if _patch_bounce(candidate):
            return True

    message = (
        "bounce-buffer macro not found; build continues with the driver value "
        "already present in the installed library"
    )
    if strict:
        raise RuntimeError("Display driver patch failed: " + message)
    print("[display-patch] warning: " + message)
    return False


# Keep this non-fatal. A changed library layout must not prevent compilation;
# the driver can still build with its own default bounce-buffer value.
patch_display_driver(strict=False)
