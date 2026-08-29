"""Patch arduinoWebSockets 2.7.2 for large LightningMaps RX frames on ESP32-S3.

LightningMaps can send an initial plain-JSON WebSocket frame well above the
library's built-in 15 KiB receive limit.  The upstream synchronous receiver
also allocates the complete frame with malloc(), which competes with TLS/LVGL
for scarce contiguous internal SRAM.  This project has 8 MiB OPI PSRAM, so we
raise the receive limit and place WebSocket payload buffers in PSRAM on ESP32.

The patch is intentionally narrow and idempotent.  It runs against PlatformIO's
installed dependency before compilation and fails the build if the expected
2.7.2 source layout cannot be patched; silently building the stock 15 KiB path
would make LightningMaps reconnect forever.
"""
from pathlib import Path
import re

Import("env")  # type: ignore[name-defined]

MAX_RX_KIB = 192
PATCH_MARKER = "LIGHTNINGMAPS_PSRAM_RX_PATCH"


def _patch_header(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    original = text

    # arduinoWebSockets 2.7.2 defines this unconditionally inside the ESP block,
    # so a -D command-line value is overwritten by the header itself.
    pattern = re.compile(
        r"(#if defined\(ESP8266\) \|\| defined\(ESP32\)\s*\n)"
        r"#define WEBSOCKETS_MAX_DATA_SIZE \([^\n]+\)"
    )
    replacement = (
        r"\1"
        f"// {PATCH_MARKER}: LightningMaps initial batches can exceed 50 KiB.\n"
        f"#define WEBSOCKETS_MAX_DATA_SIZE ({MAX_RX_KIB} * 1024)"
    )
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1 and PATCH_MARKER not in text:
        return False

    if text != original:
        path.write_text(text, encoding="utf-8")
        print(f"[websockets-patch] RX limit = {MAX_RX_KIB} KiB: patched {path}")
    else:
        print(f"[websockets-patch] RX limit = {MAX_RX_KIB} KiB: already patched")
    return True


def _patch_cpp(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    original = text

    if PATCH_MARKER not in text:
        include_old = "#elif defined(ESP32)\n#include <esp_system.h>"
        include_new = (
            "#elif defined(ESP32)\n"
            "#include <esp_system.h>\n"
            "#include <esp_heap_caps.h>  // " + PATCH_MARKER
        )
        if include_old not in text:
            return False
        text = text.replace(include_old, include_new, 1)

        alloc_old = "payload = (uint8_t *)malloc(header->payloadLen + 1);"
        alloc_new = """// LIGHTNINGMAPS_PSRAM_RX_PATCH: keep large WSS frames out of\n        // fragmented internal SRAM. WiFiClient::read() can write to PSRAM, and\n        // ESP-IDF free() is valid for heap_caps_malloc() allocations.\n#if defined(ESP32) && defined(BOARD_HAS_PSRAM)\n        payload = (uint8_t *)heap_caps_malloc(\n            header->payloadLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n        if(!payload) {\n            // A small/temporary PSRAM failure should not make the library less\n            // capable than upstream; retain the original internal-heap fallback.\n            payload = (uint8_t *)malloc(header->payloadLen + 1);\n        }\n#else\n        payload = (uint8_t *)malloc(header->payloadLen + 1);\n#endif"""
        if alloc_old not in text:
            return False
        text = text.replace(alloc_old, alloc_new, 1)

    if text != original:
        path.write_text(text, encoding="utf-8")
        print(f"[websockets-patch] ESP32 RX payloads -> PSRAM: patched {path}")
    else:
        print("[websockets-patch] ESP32 RX payloads -> PSRAM: already patched")
    return True


def patch_websockets():
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    candidates = list(libdeps.glob("WebSockets/src"))
    if not candidates:
        # Registry package folder names can vary; search for the exact library.
        candidates = [p.parent for p in libdeps.rglob("WebSocketsVersion.h") if p.parent.name == "src"]

    if not candidates:
        raise RuntimeError(
            "arduinoWebSockets dependency not found under " + str(libdeps)
        )

    for src_dir in candidates:
        header = src_dir / "WebSockets.h"
        cpp = src_dir / "WebSockets.cpp"
        if not header.exists() or not cpp.exists():
            continue
        if _patch_header(header) and _patch_cpp(cpp):
            return

    raise RuntimeError(
        "arduinoWebSockets 2.7.2 PSRAM patch failed: expected source pattern not found"
    )


patch_websockets()
