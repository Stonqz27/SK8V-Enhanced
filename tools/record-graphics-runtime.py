# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
"""Record the locally built, narrowly patched ReShade dependency; never install it."""
import hashlib
import json
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/reshade-source-6.8.0"
PIN = "18deaa52de0c425a78b329e9cb3c497281cd00ec"

def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()

def git(*args):
    return subprocess.check_output(["git", "-c", f"safe.directory={SOURCE.as_posix()}",
                                    "-C", str(SOURCE), *args])

def main():
    assert git("rev-parse", "HEAD").decode().strip() == PIN, "ReShade source pin changed"
    changed = git("diff", "--name-only").decode().splitlines()
    assert changed == ["source/dxgi/dxgi.cpp"], f"Unexpected ReShade source changes: {changed}"
    patch = git("diff", "--", "source/dxgi/dxgi.cpp")
    assert b"if ((Flags & 0x80000000u) != 0)" in patch
    assert b"return trampoline(Flags, riid, ppFactory);" in patch
    (ROOT / "tools/reshade-internal-factory.patch").write_bytes(patch)
    runtime = ROOT / "local/graphics-runtime/ReShade64.dll"
    runtime.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SOURCE / "bin/x64/Release/ReShade64.dll", runtime)
    record = dict(schema=1, built_utc=datetime.now(timezone.utc).isoformat(),
                  upstream="https://github.com/crosire/reshade", source_commit=PIN,
                  source_tag="v6.8.0", unofficial_local_build=True,
                  patch="tools/reshade-internal-factory.patch",
                  patch_sha256=sha(ROOT / "tools/reshade-internal-factory.patch"),
                  source_file_sha256=sha(SOURCE / "source/dxgi/dxgi.cpp"),
                  submodules=git("submodule", "status").decode().splitlines(),
                  runtime_path=str(runtime), runtime_sha256=sha(runtime),
                  build_configuration="Release|64-bit; VS 2026 v145; upstream Glad generator",
                  generated_glad_headers={p.relative_to(SOURCE).as_posix():sha(p)
                                          for p in sorted((SOURCE / "deps/glad/target/include").rglob("*.h"))},
                  build_log="port-evidence/build-reshade-internal-factory-fix.log",
                  license="BSD-3-Clause OR MIT; original source notices retained",
                  game_installed=False, presentation_tested=False)
    (ROOT / "port-evidence").mkdir(parents=True, exist_ok=True)
    (ROOT / "port-evidence/graphics-runtime-build.json").write_text(json.dumps(record, indent=2)+"\n")
    print(f"Recorded local ReShade runtime: {record['runtime_sha256']}; GTA unchanged")

if __name__ == "__main__":
    main()
