Import("env")

from pathlib import Path
import gzip
import shutil
import subprocess


PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
if (PROJECT_DIR.parent / "pramon").exists():
    PRAMON_DIR = PROJECT_DIR.parent / "pramon"
elif (PROJECT_DIR / "pramon").exists():
    PRAMON_DIR = PROJECT_DIR / "pramon"
else:
    PRAMON_DIR = PROJECT_DIR.parents[1] / "firmware" / "pramon"
DIST_DIR = PRAMON_DIR / "dist"
GENERATED_DIR = PROJECT_DIR / "src" / "generated"
HEADER_PATH = GENERATED_DIR / "pramon_assets.h"
SOURCE_PATH = GENERATED_DIR / "pramon_assets.cpp"


CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".txt": "text/plain; charset=utf-8",
    ".ico": "image/x-icon",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
    ".woff2": "font/woff2",
    ".woff": "font/woff",
}


def build_pramon() -> None:
    bun = shutil.which("bun")
    if bun is None:
        if not DIST_DIR.exists():
            raise RuntimeError("bun is not available and firmware/pramon/dist does not exist")
        return
    subprocess.run([bun, "run", "build"], cwd=PRAMON_DIR, check=True)


def c_identifier(index: int) -> str:
    return f"kPramonAsset{index}"


def byte_array(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        lines.append("  " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
    return "\n".join(lines)


def content_type(path: Path) -> str:
    return CONTENT_TYPES.get(path.suffix.lower(), "application/octet-stream")


def generate_assets() -> None:
    assets = []
    for file_path in sorted(DIST_DIR.rglob("*")):
        if not file_path.is_file():
            continue
        relative = file_path.relative_to(DIST_DIR).as_posix()
        route = "/" + relative
        raw = file_path.read_bytes()
        compressed = gzip.compress(raw, compresslevel=9, mtime=0)
        use_gzip = len(compressed) < len(raw)
        payload = compressed if use_gzip else raw
        assets.append({
            "route": route,
            "content_type": content_type(file_path),
            "payload": payload,
            "gzip": use_gzip,
        })

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(
        """#pragma once

#include <Arduino.h>

struct PramonAsset {
  const char *path;
  const char *contentType;
  const uint8_t *data;
  size_t length;
  bool gzip;
};

extern const PramonAsset kPramonAssets[];
extern const size_t kPramonAssetCount;
""",
        encoding="utf-8",
    )

    source = [
        '#include "pramon_assets.h"',
        "",
        "#include <pgmspace.h>",
        "",
        "namespace {",
        "",
    ]
    for index, asset in enumerate(assets):
        source.append(f"const uint8_t {c_identifier(index)}[] PROGMEM = {{")
        source.append(byte_array(asset["payload"]))
        source.append("};")
        source.append("")
    source.append("}  // namespace")
    source.append("")
    source.append("const PramonAsset kPramonAssets[] = {")
    for index, asset in enumerate(assets):
        source.append(
            f'  {{"{asset["route"]}", "{asset["content_type"]}", {c_identifier(index)}, '
            f'sizeof({c_identifier(index)}), {"true" if asset["gzip"] else "false"}}},'
        )
    source.append("};")
    source.append("")
    source.append("const size_t kPramonAssetCount = sizeof(kPramonAssets) / sizeof(kPramonAssets[0]);")
    source.append("")
    SOURCE_PATH.write_text("\n".join(source), encoding="utf-8")


build_pramon()
generate_assets()
