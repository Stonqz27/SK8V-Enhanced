"""Installer regression checks use temporary game folders, never a real installation."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from _common import REPO
import enhanced_setup as setup


class InstallerTests(unittest.TestCase):
    def setUp(self):
        folder = REPO/'build/setup-tests'
        folder.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=folder)
        self.root = Path(self.temp.name).resolve()
        self.game = self.root/'game'
        self.game.mkdir()
        self.source = self.root/'source'
        self.source.mkdir()
        self.values = dict(DataRoot=str(self.root/'assets'), AudioCache=str(self.root/'audio'),
                           PedCache=str(self.root/'peds'), WorldCache='', Stance='Regular', Line='')
        self.preflight = patch.object(setup, 'preflight', lambda game, dependencies=True: Path(game).resolve())
        self.closed = patch.object(setup, 'closed', lambda: None)
        self.preflight.start()
        self.closed.start()

    def tearDown(self):
        self.preflight.stop()
        self.closed.stop()
        self.temp.cleanup()

    def payload(self, names=('SkateVEnhanced.asi', 'SkateVRuntime.dll')):
        result = {}
        for name in names:
            path = self.source/name
            path.write_bytes(('new-'+name).encode())
            result[name] = path
        return result

    def test_fresh_install_and_restore_keep_prepared_data(self):
        asset = self.root/'assets'
        asset.mkdir()
        (asset/'owned.txt').write_text('keep')
        setup.install(self.game, self.payload(), self.values, 'test')
        self.assertEqual(setup.read(self.game/setup.MANIFEST)['state'], 'installed')
        ini = (self.game/'SkateVLegacy.ini').read_text()
        self.assertIn(str(asset), ini)
        self.assertNotIn('{DataRoot}', ini)
        setup.restore(self.game)
        self.assertFalse((self.game/'SkateVEnhanced.asi').exists())
        self.assertFalse((self.game/'SkateVRuntime.dll').exists())
        self.assertFalse((self.game/'SkateVLegacy.ini').exists())
        self.assertEqual((asset/'owned.txt').read_text(), 'keep')

    def test_graphics_conflict_rejected_before_changes(self):
        original = self.game/'dxgi.dll'
        original.write_bytes(b'other graphics mod')
        with self.assertRaisesRegex(RuntimeError, 'Existing graphics mod'):
            setup.install(self.game, self.payload(('dxgi.dll',)), self.values, 'test')
        self.assertEqual(original.read_bytes(), b'other graphics mod')
        self.assertFalse((self.game/setup.MANIFEST).exists())

    def test_explicit_graphics_replacement_restores_original(self):
        original = self.game/'dxgi.dll'
        original.write_bytes(b'old graphics')
        setup.install(self.game, self.payload(('dxgi.dll',)), self.values, 'test', True)
        setup.restore(self.game)
        self.assertEqual(original.read_bytes(), b'old graphics')

    def test_upgrade_preserves_preferences_and_restore_previous_pair(self):
        (self.game/'SkateVEnhanced.asi').write_bytes(b'old host')
        (self.game/'SkateVRuntime.dll').write_bytes(b'old runtime')
        (self.game/'SkateVLegacy.ini').write_text('[SkateV]\nDifficulty=Hardcore\nLiveClip=1\nDrawSkater=1\nPedPoseCollision=1\nAudioOutput=SDL\n')
        setup.install(self.game, self.payload(), self.values, 'test')
        ini = setup.setup_engine.ini_values((self.game/'SkateVLegacy.ini').read_text())
        self.assertEqual(ini['Difficulty'], 'Hardcore')
        for key, value in setup.FORCED.items():
            self.assertEqual(ini[key], value)
        setup.restore(self.game)
        self.assertEqual((self.game/'SkateVEnhanced.asi').read_bytes(), b'old host')
        self.assertEqual((self.game/'SkateVRuntime.dll').read_bytes(), b'old runtime')

    def test_restore_refuses_modifications_by_another_mod(self):
        setup.install(self.game, self.payload(), self.values, 'test')
        (self.game/'SkateVRuntime.dll').write_bytes(b'changed later')
        with self.assertRaisesRegex(RuntimeError, 'changed by another mod'):
            setup.restore(self.game)
        self.assertEqual((self.game/'SkateVRuntime.dll').read_bytes(), b'changed later')
        self.assertEqual(setup.read(self.game/setup.MANIFEST)['state'], 'installed')

    def test_failure_after_first_file_automatically_restores(self):
        old = self.game/'SkateVRuntime.dll'
        old.write_bytes(b'old runtime')
        copy = setup.shutil.copy2
        def fail(source, destination, *args, **kwargs):
            if Path(destination).name == 'SkateVEnhanced.asi.sk8v-stage':
                raise OSError('injected disk failure before host activation')
            return copy(source, destination, *args, **kwargs)
        with patch.object(setup.shutil, 'copy2', fail), self.assertRaisesRegex(OSError, 'injected disk failure'):
            setup.install(self.game, self.payload(), self.values, 'test')
        self.assertEqual(old.read_bytes(), b'old runtime')
        self.assertFalse((self.game/'SkateVEnhanced.asi').exists())
        self.assertEqual(setup.read(self.game/setup.MANIFEST)['state'], 'restored')

    def test_path_escape_and_unrelated_files_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'escapes'):
            setup.child(self.game, '../outside.txt')
        files = self.payload()
        files['GTA5_Enhanced.exe'] = next(iter(files.values()))
        with self.assertRaisesRegex(RuntimeError, 'Unexpected installation target'):
            setup.install(self.game, files, self.values, 'test')
        self.assertFalse((self.game/'GTA5_Enhanced.exe').exists())

    def test_manifest_cannot_restore_unrelated_game_file(self):
        setup.install(self.game, self.payload(), self.values, 'test')
        manifest = setup.read(self.game/setup.MANIFEST)
        manifest['files'][0]['target'] = 'GTA5_Enhanced.exe'
        setup.write(self.game/setup.MANIFEST, manifest)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected restoration target'):
            setup.restore(self.game)


if __name__ == '__main__':
    unittest.main()
