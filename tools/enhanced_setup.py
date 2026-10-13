"""Portable Enhanced preparation and reversible installation. No retail payload ships.

Run without arguments for guided setup, or --help for unattended preparation,
preflight and restoration. Generated assets stay in the extracted release folder.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback
import uuid

from _common import REPO, sha256_file
import setup_engine
import xiso

GAME_VERSION = '1.0.1158.16'
MANIFEST = 'SK8V-Enhanced.installed.json'
BIN = REPO / 'tools/bin'
PAYLOAD = REPO / 'payload'
CW_FILES = ('CodeWalker.Core.dll', 'SharpDX.dll', 'SharpDX.Mathematics.dll', 'ShadersGen9Conversion.xml')
FORCED = dict(Runtime='1', PosePed='1', BoardNative='1', DrawSkater='0', LiveClip='0',
              PedPoseCollision='0', BackgroundPrepare='1', AudioOutput='Gta', BoardTracks='0')
INSTALL_FILES = {'SkateVEnhanced.asi', 'SkateVRuntime.dll', 'SK8VHudBridge.addon64', 'dxgi.dll',
                 'ReShade.ini', 'SkateVLegacy.ini', 'mods/update/update.rpf',
                 'mods/update/x64/dlcpacks/skatev/dlc.rpf'}
EVENTS = False
STAGES = {'disc': (5, 15, 'Reading your Skate 3 disc'),
          'skate': (15, 40, 'Preparing skating animations and HUD'),
          'audio': (40, 60, 'Preparing Skate audio'),
          'peds': (60, 72, 'Reading GTA characters'),
          'board': (72, 82, 'Building the Enhanced skateboard'),
          'pack': (82, 90, 'Preparing the board and weapon pack')}


def event(title, progress, phase='install'):
    """Coarse stage progress, not an estimate of remaining seconds or bytes."""
    if EVENTS:
        print('@sk8v ' + json.dumps(dict(title=title, progress=progress, phase=phase)), flush=True)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.new')
    temp.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temp, path)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def running():
    if os.name != 'nt':
        return False
    result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True, check=True)
    return any('"' + name.lower() + '"' in result.stdout.lower()
               for name in ('GTA5.exe', 'GTA5_Enhanced.exe'))


def closed():
    require(not running(), 'Close GTA completely before setup or restore.')


def child(root, relative):
    root = Path(root).resolve()
    path = (root / relative).resolve()
    require(path != root and path.is_relative_to(root), 'Path escapes its installation directory: ' + str(relative))
    return path


def preflight(game, dependencies=True):
    game = Path(game).resolve()
    closed()
    exe = game / 'GTA5_Enhanced.exe'
    require(exe.is_file(), 'Select the folder containing GTA5_Enhanced.exe, not GTA Legacy.')
    require(setup_engine.file_version(exe) == GAME_VERSION,
            f'This release targets Enhanced {GAME_VERSION}; other patches are not supported.')
    require((game / 'update/update.rpf').is_file(), 'The GTA installation is incomplete: update/update.rpf is missing.')
    conflicts = [name for name in ('SkateVLegacy.asi', 'NativeFranklin.asi', 'SkateGTA.asi') if (game / name).exists()]
    require(not conflicts, 'Move these other skating hosts out of the game folder first: ' + ', '.join(conflicts))
    if dependencies:
        for name in ('ScriptHookV.dll', 'dinput8.dll', 'OpenRPF.asi'):
            require((game / name).is_file(), name + ' is missing. Install Enhanced-compatible dependencies; see START-HERE.md.')
    return game


def verify_package():
    manifest = read(REPO / 'release.json')
    require(manifest['game_version'] == GAME_VERSION, 'Wrong package target.')
    for relative, digest in manifest['files'].items():
        file = child(REPO, relative)
        require(file.is_file() and sha256_file(file) == digest, 'Package file missing or changed: ' + relative)
    return manifest


def command(*args):
    result = subprocess.run(list(map(str, args)), cwd=REPO)
    require(result.returncode == 0, 'Preparation command failed; see the preceding message. Rerun setup to resume.')


def py(script, *args):
    command(sys.executable, '-B', REPO / 'tools' / script, *args)


def prepare(game, skate, codewalker, stance):
    require(sys.version_info >= (3, 11), 'Python 3.11 or later is required.')
    import numpy  # noqa: F401
    import PIL  # noqa: F401
    require(shutil.which('dotnet'), 'Install Microsoft .NET 9 x64 Runtime, then reopen setup; see START-HERE.md.')
    runtimes = subprocess.check_output(['dotnet', '--list-runtimes'], text=True)
    require('Microsoft.NETCore.App 9.' in runtimes, 'Microsoft .NET 9 x64 Runtime is required; see START-HERE.md.')
    codewalker = Path(codewalker).resolve()
    for name in CW_FILES:
        require((codewalker / name).is_file(), f'Select a Gen9-capable CodeWalker directory containing {name}.')
    skate = Path(skate).resolve()
    require(skate.exists(), 'Skate 3 ISO/extracted folder does not exist.')
    if skate.is_dir():
        require(all((skate / name).is_file() for name in xiso.NEEDED), 'Incomplete Skate 3 Xbox 360 disc folder.')
    signature = dict(game=str(game), version=GAME_VERSION, skate=str(skate), stance=stance,
                     skate_stat=[skate.stat().st_size, skate.stat().st_mtime_ns],
                     game_stat=[(game/'update/update.rpf').stat().st_size, (game/'update/update.rpf').stat().st_mtime_ns],
                     codewalker={name: sha256_file(codewalker/name) for name in CW_FILES},
                     release=sha256_file(REPO/'release.json'))
    if skate.is_dir():
        signature['disc_files'] = {name: [(skate/name).stat().st_size, (skate/name).stat().st_mtime_ns]
                                   for name in xiso.NEEDED}
        signature['xex_sha256'] = sha256_file(skate/'default.xex')
    import hashlib
    key = hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()[:16]
    work = REPO / 'local' / ('setup-' + key)
    work.mkdir(parents=True, exist_ok=True)
    print('Prepared data will remain here:', work, flush=True)
    helper = work / 'helper'
    helper.mkdir(exist_ok=True)
    for name in CW_FILES:
        shutil.copy2(codewalker/name, helper/name)
    for file in (REPO/'tools/enhanced-helper').iterdir():
        if file.is_file():
            shutil.copy2(file, helper/file.name)

    def asset(*args):
        command('dotnet', helper/'EnhancedAssets.dll', '--output-root', work, *args)

    def step(name, function):
        start, finish, title = STAGES[name]
        event(title, start)
        closed()
        folder = work/name
        receipt = work/(name+'.json')
        if receipt.is_file():
            records = read(receipt)['files']
            if all(child(work, p).is_file() and sha256_file(child(work, p)) == h for p, h in records.items()):
                print('Already prepared:', name, flush=True)
                event(title + ' - cached', finish)
                return folder
        if folder.exists():
            folder.rename(work/(name+'.incomplete-'+uuid.uuid4().hex[:8]))
        folder.mkdir()
        print('Preparing:', name, flush=True)
        files = function(folder)
        write(receipt, {'files': {str(p.relative_to(work)): sha256_file(p) for p in files}})
        event(title + ' - complete', finish)
        return folder

    def disc_stage(folder):
        if skate.is_dir():
            # Record the caller's disc identity; never alter its extracted files.
            write(folder/'source.json', {'path': str(skate), 'xex_sha256': sha256_file(skate/'default.xex')})
            return [folder/'source.json']
        xiso.extract_needed(skate, folder/'disc')
        return [folder/'disc/default.xex']
    disc_step = step('disc', disc_stage)
    disc = skate if skate.is_dir() else disc_step/'disc'

    def skate_stage(folder):
        py('convert-skate-data.py', '--xex', disc/'default.xex', '--out', folder/'data')
        assets = folder/'data/assets'
        py('prepare-board.py', '--assets', assets)
        for movie in ('homscoring', 'trickanalyser', 'chyron'):
            arguments = ['--game', disc, '--assets', assets]
            if movie != 'homscoring':
                arguments += ['--movie', movie]
            py('prepare-hom-hud.py', *arguments)
        py('prepare-hom-xray.py', '--game', disc, '--assets', assets)
        return [folder/'data/receipt.json', assets/'private/hud/runtime/trickdisplay.json',
                assets/'private/board/board-materials.json']
    converted = step('skate', skate_stage)
    assets = converted/'data/assets'

    def audio_stage(folder):
        py('prepare-skate-audio.py', '--skate-data', disc, '--out', folder/'cache', '--decoder', BIN/'skate-xma.exe')
        py('prepare-audio-tuning.py', '--collections', assets/'private/stock/skater-collections.json',
           '--out', folder/'cache/grain-tuning.bin', '--component-out', folder/'cache/component-tuning.bin',
           '--xex', disc/'default.xex', '--xex-tool', BIN/'xex_image.exe')
        return [folder/'cache/manifest.json', folder/'cache/grain-tuning.bin', folder/'cache/component-tuning.bin']
    audio = step('audio', audio_stage)/'cache'

    def ped_stage(folder):
        asset('skeletons', game, folder/'peds')
        return [folder/'peds/extraction-receipt.json'] + [folder/'peds'/p/'skeleton.json'
                                                       for p in ('player_zero', 'player_one', 'player_two')]
    peds = step('peds', ped_stage)/'peds'

    def board_stage(folder):
        asset('extract', game, 'x64w.rpf', 'pil_p_para_bag_pilot_s.ydr', folder/'reference.ydr')
        asset('shader-reference', folder/'reference.ydr', folder/'shader-reference.ydr.xml')
        py('prepare-board-native.py', '--board', assets/'private/board/board-materials.json',
           '--lighting', assets/'private/character-lighting.json', '--shader-reference', folder/'shader-reference.ydr.xml',
           '--rage', BIN/'rage.exe', '--out', folder/'native')
        asset('convert-board', folder/'native/skatev_board.ydr', folder/'skatev_board.ydr')
        return [folder/'skatev_board.ydr', folder/'native/skatev_board.ytyp']
    board = step('board', board_stage)

    def pack_stage(folder):
        for name in ('weapons.meta', 'weaponanimations.meta'):
            asset('extract', game, 'update/update.rpf', 'common/data/ai/'+name, folder/name)
        py('build-board-weapon.py', folder/'weapons.meta', folder/'weaponanimations.meta', folder/'clip/weapon')
        command(BIN/'live_clip.exe', peds, folder/'clip', folder/'clip/layout.h')
        asset('stage-pack', folder/'clip/dlc.rpf', board/'skatev_board.ydr', board/'native/skatev_board.ytyp', folder/'dlc.rpf')
        return [folder/'dlc.rpf', folder/'dlc.rpf.receipt.json']
    pack = step('pack', pack_stage)/'dlc.rpf'
    manifest = read(audio/'manifest.json')
    require(manifest.get('complete') and not manifest.get('problems'), 'Audio preparation is incomplete.')
    data = dict(DataRoot=str(assets), AudioCache=str(audio), PedCache=str(peds), WorldCache='', Stance=stance, Line='')
    return work, pack, data, asset


def settings(values, old=''):
    template = (REPO/'tools/SkateVLegacy.ini.template').read_text(encoding='utf-8')
    value = setup_engine.render_ini(template, {**values, **FORCED}, old, ['Stance', 'Line'])
    # Safety choices must not inherit incompatible Legacy values on upgrade.
    lines = []
    for line in value.splitlines():
        key = line.partition('=')[0]
        lines.append(f'{key}={FORCED[key]}' if key in FORCED else line)
    return '\r\n'.join(lines)+'\r\n'


def restore(game):
    event('Checking the previous installation', 5, 'restore')
    closed()
    game = Path(game).resolve()
    path = game/MANIFEST
    saved = read(path)
    require(Path(saved['game']).resolve() == game, 'Installation manifest belongs to a different game folder.')
    require(saved['state'] in ('installed', 'installing', 'failed'), 'This installation is already restored.')
    # Verify every restoration input before touching active files.
    for record in saved['files']:
        require(record['target'] in INSTALL_FILES, 'Unexpected restoration target in manifest.')
        target = child(game, record['target'])
        if record['backup']:
            backup = child(game, record['backup'])
            require(backup.is_file() and sha256_file(backup) == record['previous'], 'Backup changed: '+str(backup))
        if target.is_file():
            digest = sha256_file(target)
            require(record['mutable'] or digest in (record['installed'], record['previous']),
                    'A file was changed by another mod; restore manually from the backup: '+str(target))
    retired = child(game, saved['backup_dir']+'/retired')
    retired.mkdir(exist_ok=True)
    for index, record in reversed(list(enumerate(saved['files']))):
        event('Restoring ' + record['target'], 10 + int(85*(len(saved['files'])-index)/max(1, len(saved['files']))), 'restore')
        target = child(game, record['target'])
        if target.is_file():
            shutil.copy2(target, retired/f'{index}-{target.name}')
        if record['backup']:
            target.parent.mkdir(parents=True, exist_ok=True)
            temp = target.with_name(target.name+'.sk8v-restore')
            shutil.copy2(child(game, record['backup']), temp)
            os.replace(temp, target)
        else:
            target.unlink(missing_ok=True)
    saved['state'] = 'restored'
    write(path, saved)
    print('Previous mod files restored. Prepared assets and backups were retained.')


def install(game, files, values, version, allow_graphics=False):
    event('Checking installation targets and backups', 96)
    game = preflight(game)
    manifest = game/MANIFEST
    if manifest.exists():
        prior = read(manifest)
        require(Path(prior['game']).resolve() == game, 'Installation manifest belongs to a different game folder.')
        require(prior['state'] in ('installed', 'restored'), 'Incomplete install: run --restore first.')
    owned = {r['target']: r for r in read(manifest)['files']} if manifest.exists() else {}
    for relative, source in files.items():
        require(relative in INSTALL_FILES, 'Unexpected installation target: '+relative)
        target = child(game, relative)
        require(Path(source).is_file(), 'Missing installation payload: '+str(source))
        if target.is_file() and relative in ('dxgi.dll', 'ReShade.ini', 'SK8VHudBridge.addon64'):
            known = owned.get(relative)
            require(allow_graphics or known and (known['mutable'] or sha256_file(target) == known['installed']),
                    'Existing graphics mod: '+relative+'. Use --allow-graphics-replacement only after reviewing START-HERE.md.')
    require(shutil.disk_usage(game).free > sum(Path(p).stat().st_size for p in files.values())*2 + 256*1024**2,
            'Not enough free disk space for installation staging and backups.')
    backup_rel = 'SK8V-Enhanced-backups/'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8]
    backup = child(game, backup_rel)
    backup.mkdir(parents=True)
    old = (game/'SkateVLegacy.ini').read_text(encoding='utf-8-sig') if (game/'SkateVLegacy.ini').exists() else ''
    ini = backup/'prepared-settings.ini'
    ini.write_text(settings(values, old), encoding='utf-8', newline='')
    files = {**files, 'SkateVLegacy.ini': ini}
    # Make the ASI active last; restore removes it first.
    files = dict(sorted(files.items(), key=lambda item: item[0] == 'SkateVEnhanced.asi'))
    records = []
    for index, (relative, source) in enumerate(files.items()):
        target = child(game, relative)
        digest = sha256_file(target) if target.is_file() else None
        saved = backup/f'{index}-{target.name}' if digest else None
        if saved:
            shutil.copy2(target, saved)
            require(sha256_file(saved) == digest, 'Backup verification failed.')
        records.append(dict(target=relative, installed=sha256_file(source), previous=digest,
                            backup=str(saved.relative_to(game)) if saved else None,
                            mutable=relative in ('SkateVLegacy.ini', 'ReShade.ini')))
    if manifest.exists():
        shutil.copy2(manifest, backup/'previous-manifest.json')
    saved = dict(schema=1, game=str(game), version=version, state='installing', backup_dir=backup_rel, files=records)
    write(manifest, saved)
    try:
        for index, record in enumerate(records):
            event('Installing ' + record['target'], 97 + int(2*index/max(1, len(records))))
            closed()
            target = child(game, record['target'])
            require((sha256_file(target) if target.is_file() else None) == record['previous'],
                    'Target changed during installation: '+record['target'])
            target.parent.mkdir(parents=True, exist_ok=True)
            temp = target.with_name(target.name+'.sk8v-stage')
            shutil.copy2(files[record['target']], temp)
            require(sha256_file(temp) == record['installed'], 'Staging checksum failed.')
            os.replace(temp, target)
        saved['state'] = 'installed'
        write(manifest, saved)
    except BaseException:
        saved['state'] = 'failed'
        write(manifest, saved)
        try:
            restore(game)
        except Exception as error:
            print('Automatic restore requires attention:', error)
        raise
    print('Installed', version, '\nBackup:', backup)
    print('Keep the extracted setup folder: GTA reads your locally prepared data from it.')


def prompted(title):
    return input(title+': ').strip().strip('"')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gta', type=Path)
    parser.add_argument('--skate', type=Path)
    parser.add_argument('--codewalker', type=Path)
    parser.add_argument('--stance', choices=['Regular', 'Goofy'], default='Regular')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--preflight', action='store_true')
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--allow-graphics-replacement', action='store_true')
    parser.add_argument('--events', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    global EVENTS
    EVENTS = args.events
    guided = not any((args.gta, args.skate, args.codewalker, args.restore, args.preflight))
    if guided:
        print('SK8V Enhanced setup. Close GTA. Read START-HERE.md for dependencies.')
        args.gta = Path(prompted('GTA Enhanced folder'))
        args.skate = Path(prompted('Your Skate 3 Xbox 360 ISO or extracted disc folder'))
        args.codewalker = Path(prompted('Gen9 CodeWalker folder (CodeWalker.Core.dll and shader XML)'))
    require(args.gta, '--gta is required.')
    if args.restore:
        restore(args.gta)
        event('Previous files restored', 100, 'complete')
        return
    event('Verifying the release', 0)
    package = verify_package()
    event('Checking GTA Enhanced and dependencies', 2)
    game = preflight(args.gta)
    if args.preflight:
        print('Enhanced version, dependencies and release checksums passed. GTA unchanged.')
        event('Checks passed; GTA unchanged', 100, 'complete')
        return
    require(args.skate and args.codewalker, '--skate and --codewalker are required.')
    work, pack, data, asset = prepare(game, args.skate, args.codewalker, args.stance)
    if args.prepare_only:
        write(work/'prepared.json', dict(pack=str(pack), settings=data))
        print('Assets prepared; GTA unchanged.')
        event('Assets prepared; GTA unchanged', 100, 'complete')
        return
    source = game/'mods/update/update.rpf'
    if not source.is_file():
        source = game/'update/update.rpf'
    # Rebuild from the current DLC list on every install, keeping other mods.
    require(shutil.disk_usage(work).free > source.stat().st_size+256*1024**2,
            'Not enough space in the setup folder to stage the modded update archive.')
    folder = work/('update-'+uuid.uuid4().hex[:8])
    folder.mkdir()
    event('Preserving the GTA DLC list', 90)
    asset('stage-update', game, folder/'update.rpf')
    event('DLC archive checked', 96)
    files = {name: PAYLOAD/name for name in ('SkateVEnhanced.asi', 'SkateVRuntime.dll', 'SK8VHudBridge.addon64', 'dxgi.dll')}
    files.update({'ReShade.ini': REPO/'tools/SkateVEnhanced.ReShade.ini',
                  'mods/update/update.rpf': folder/'update.rpf',
                  'mods/update/x64/dlcpacks/skatev/dlc.rpf': pack})
    if guided:
        print('Setup will install the mod, its patched ReShade, and a modded DLC-list copy. Originals are backed up.')
        if any((game/n).exists() for n in ('dxgi.dll', 'ReShade.ini', 'SK8VHudBridge.addon64')):
            require(prompted('Replace existing graphics files with backups? Type YES') == 'YES', 'Installation cancelled; assets retained.')
            args.allow_graphics_replacement = True
        require(prompted('Install prepared mod? Type YES') == 'YES', 'Installation cancelled; assets retained.')
    install(game, files, data, package['version'], args.allow_graphics_replacement)
    print('Launch Story Mode with BattlEye disabled. Select Skateboard in the melee wheel; Y mounts/dismounts.')
    event('Ready to skate', 100, 'complete')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        traceback.print_exc()
        print('Setup stopped:', error, file=sys.stderr)
        event(str(error), 0, 'error')
        sys.exit(2)
