"""Package only Enhanced binaries, preparation code and notices; never local game data.

python tools/build-enhanced-release.py --version 1.1.8-enhanced.1-preview
The optional --graphics-runtime points to the verified, patched ReShade DLL.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from _common import REPO, sha256_file

TOOLS = ('enhanced_setup.py', 'enhanced-setup.ps1', 'get-python.ps1', 'setup_engine.py', '_common.py',
         'xiso.py', 'convert-skate-data.py', 'prepare-board.py', 'prepare-board-native.py',
         'prepare-skate-audio.py', 'prepare-audio-tuning.py', 'prepare-hom-hud.py', 'prepare-hom-xray.py',
         'build-board-weapon.py', 'SkateVLegacy.ini.template', 'SkateVEnhanced.ReShade.ini')
BINARIES = {'skate-xma.exe': 'tools/bin/skate-xma.exe', 'xex_image.exe': 'tools/bin/xex_image.exe',
            'live_clip.exe': 'rust/target/release/live_clip.exe', 'rage.exe': 'build/tools/rage.exe'}
PAYLOAD = {'SkateVEnhanced.asi': 'build/enhanced-gameplay-package/SkateVEnhanced.asi',
           'SK8VHudBridge.addon64': 'build/enhanced-gameplay-package/SK8VHudBridge.addon64',
           'SkateVRuntime.dll': 'build/runtime/SkateVRuntime.dll'}
RETAIL = {'.big', '.xex', '.rpf', '.ydr', '.ydd', '.yft', '.ytd', '.ytyp', '.ymap', '.ybn', '.ycd',
          '.gfx', '.abin', '.glb', '.mxb', '.svwc', '.svpc', '.meta', '.pdb'}


def copy(source, destination):
    source = Path(source)
    if not source.is_file():
        raise RuntimeError('Build or supply missing release input: '+str(source))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def audit(root):
    files = sorted(p for p in root.rglob('*') if p.is_file())
    forbidden = {'CodeWalker.Core.dll', 'SharpDX.dll', 'SharpDX.Mathematics.dll', 'ShadersGen9Conversion.xml',
                 'ScriptHookV.dll', 'dinput8.dll', 'OpenRPF.asi'}
    needles = [p.lower().encode() for p in (str(Path.home()), str(Path.home()).replace('\\', '/'))]
    for file in files:
        if file.suffix.lower() in RETAIL or file.name in forbidden or 'local' in file.relative_to(root).parts:
            raise RuntimeError('Forbidden release payload: '+str(file.relative_to(root)))
        data = file.read_bytes().lower()
        if any(needle in data for needle in needles):
            raise RuntimeError('Personal source path in release file; rebuild with path remapping: '+str(file))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--graphics-runtime', type=Path, default=REPO/'local/graphics-runtime/ReShade64.dll')
    parser.add_argument('--graphics-license', type=Path, default=REPO/'local/graphics-runtime/LICENSE.md')
    args = parser.parse_args()
    if not all(c.isalnum() or c in '.-' for c in args.version):
        parser.error('Version may contain letters, numbers, dots and hyphens only.')
    root = REPO/'build/release'/('SK8V-Enhanced-'+args.version)
    if root.exists():
        raise RuntimeError('Preserve the existing release directory before rebuilding: '+str(root))
    for name, source in PAYLOAD.items():
        copy(REPO/source, root/'payload'/name)
    copy(args.graphics_runtime, root/'payload/dxgi.dll')
    copy(args.graphics_license, root/'licenses/ReShade-LICENSE.md')
    for name, source in BINARIES.items():
        copy(REPO/source, root/'tools/bin'/name)
    for name in TOOLS:
        copy(REPO/'tools'/name, root/'tools'/name)
    helper = REPO/'tools/enhanced-assets/bin/Release/net9.0'
    for name in ('EnhancedAssets.dll', 'EnhancedAssets.deps.json', 'EnhancedAssets.runtimeconfig.json'):
        copy(helper/name, root/'tools/enhanced-helper'/name)
    for name in ('LICENSE', 'NOTICE', 'upstreams.lock.json', 'CHANGELOG.md'):
        copy(REPO/name, root/name)
    copy(REPO/'docs/PLAYER-SETUP.md', root/'START-HERE.md')
    copy(REPO/'rust/skate-xma/COPYING.LGPL', root/'licenses/COPYING.LGPL')
    # Separate LGPL decoder source accompanies its executable.
    for source in (REPO/'rust/skate-xma').rglob('*'):
        if source.is_file() and not any(part in ('target', '__pycache__') for part in source.parts):
            copy(source, root/'source/skate-xma'/source.relative_to(REPO/'rust/skate-xma'))
    copy(REPO/'rust/Cargo.lock', root/'source/Cargo.lock')
    (root/'SK8V Enhanced Setup.cmd').write_text('@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\\enhanced-setup.ps1" %*\r\npause\r\n', encoding='ascii')
    # Reuse upstream's carefully scoped converter-tool snapshot, not its Legacy installer.
    spec = importlib.util.spec_from_file_location('legacy_package', REPO/'tools/build-release.py')
    legacy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(legacy)
    legacy.donor_tools(root)
    subprocess.run([sys.executable, REPO/'tools/third-party-notices.py', '--include-rage-cli', '--out', root/'THIRD-PARTY-NOTICES.txt'], check=True)
    files = audit(root)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
    manifest = dict(schema=1, version=args.version, game_version='1.0.1158.16', source_commit=revision,
                    upstream_version='1.1.8', prerelease=True, new_live_gameplay_test=False,
                    assets_included=False, files={p.relative_to(root).as_posix(): sha256_file(p) for p in files})
    (root/'release.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    files = audit(root)
    (root/'SHA256SUMS.txt').write_text(''.join(f'{sha256_file(p)}  {p.relative_to(root).as_posix()}\n' for p in files), encoding='ascii')
    archive = root.parent/(root.name+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for file in sorted(p for p in root.rglob('*') if p.is_file()):
            target.write(file, Path(root.name)/file.relative_to(root))
    sums = archive.parent/(archive.name+'.sha256')
    sums.write_text(f'{sha256_file(archive)}  {archive.name}\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size, 'files': len(files)+1,
                      'sha256': sha256_file(archive)}, indent=2))


if __name__ == '__main__':
    main()
