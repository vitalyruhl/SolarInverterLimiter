"""Consumer build checks; [MOCKED!] SCons and the external CM UI subprocess only."""
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import shlex
import shutil
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/precompile_wrapper.py"
DEVELOPMENT_URI = "file://.local/cm-wifi-sleep-4.10.0-package"


class BuildEnvironment(dict):
    def __init__(self, root):
        super().__init__(PIOENV="usb", BUILD_FLAGS="-DCM_ENABLE_LIVE_UI=0 -DCM_ENABLE_SETTINGS_UI=1 -DCM_ENABLE_LOGGING=0")
        self.root = root
        self.development = False

    def subst(self, key):
        return str(self.root / ".pio/libdeps") if key == "$PROJECT_LIBDEPS_DIR" else str(self.root)

    def GetProjectOption(self, key, default=None):
        if key == "lib_deps":
            return [DEVELOPMENT_URI] if self.development else ["vitaly.ruhl/ESP32 Configuration Manager@4.9.0"]
        return default

    def Replace(self, **kwargs):
        self.update(kwargs)

    def ParseFlags(self, flags):
        return {"CPPDEFINES": [tuple(flag[2:].split("=", 1)) for flag in shlex.split(flags)]}


class ConsumerPreparationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sil-consumer-ui-", dir="C:/Temp")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/prepare_cm_development.py', self.root / 'tools/prepare_cm_development.py')
        self.library = self.root / ".pio/libdeps/usb/ESP32 Configuration Manager"
        for name in ("src", "tools", "webui"):
            (self.library / name).mkdir(parents=True)
        (self.library / "library.json").write_text(json.dumps({"name": "ESP32 Configuration Manager", "version": "4.9.0"}))
        (self.library / ".piopm").write_text(json.dumps({"version": "4.9.0", "spec": {"owner": "vitaly.ruhl"}}))
        (self.library / "src/ConfigManager.h").write_text('#define CONFIGMANAGER_VERSION "4.9.0"')
        (self.library / "webui/package.json").write_text(json.dumps({"version": "4.9.0"}))
        (self.library / "tools/preCompile_script.py").write_text("# [MOCKED!] External published builder boundary\n")
        # [MOCKED!] Published flag resolver interface; production build uses the real registry module.
        (self.library / "tools/webui_features.py").write_text("""def feature_values(env):
    values={'CM_ENABLE_LIVE_UI':'1','CM_ENABLE_SETTINGS_UI':'1','CM_EMBED_WEBUI':'1','CM_ENABLE_LOGGING':'1','CM_ENABLE_VERBOSE_LOGGING':'0'}
    values.update(dict(env.ParseFlags(env['BUILD_FLAGS'])['CPPDEFINES']))
    return values

def source_fingerprint(root, features):
    return 'fixture-source-and-flags'
""")
        self.asset = self.library / "src/html_content.h"
        self.asset.write_text("#define CM_WEBUI_LIVE_UI 1\n#define CM_WEBUI_SETTINGS_UI 1\n")
        self.environment = BuildEnvironment(self.root)
        self.calls = []

    def builder(self, args, **kwargs):
        self.calls.append((args, kwargs))
        self.asset.write_text("#define CM_WEBUI_LIVE_UI 0\n#define CM_WEBUI_SETTINGS_UI 1\n")
        cache = self.library / ".pio/cm-webui-build.json"
        cache.parent.mkdir(exist_ok=True)
        features = {key: kwargs['env'][key] for key in ('CM_ENABLE_LIVE_UI', 'CM_ENABLE_SETTINGS_UI', 'CM_EMBED_WEBUI', 'CM_ENABLE_LOGGING', 'CM_ENABLE_VERBOSE_LOGGING')}
        cache.write_text(json.dumps({'fingerprint':'fixture-source-and-flags', 'features':features,
                                    'header_sha256':hashlib.sha256(self.asset.read_bytes()).hexdigest()}))

    def run_hook(self, builder=None, without_file=False):
        script_module = types.ModuleType("SCons.Script")
        def inject(name):
            sys._getframe(1).f_globals[name] = self.environment
        script_module.Import = inject
        with patch.dict(sys.modules, {"SCons": types.ModuleType("SCons"), "SCons.Script": script_module}), patch("subprocess.run", side_effect=builder or self.builder):
            if without_file:
                # SCons SConscript provides Import/env but does not define __file__.
                globals_ = {'__name__': '__scons__'}
                exec(compile(SCRIPT.read_text(encoding='utf-8'), str(SCRIPT), 'exec'), globals_)
                self.assertNotIn('__file__', globals_)
            else:
                runpy.run_path(str(SCRIPT))

    def test_compiler_flags_rebuild_default_asset_in_consumer_package(self):
        self.run_hook()
        self.assertEqual(len(self.calls), 1)
        args, options = self.calls[0]
        self.assertEqual(Path(options['cwd']).resolve(), self.library.resolve())
        self.assertEqual(Path(args[-1]).name, 'preCompile_script.py')
        self.assertEqual(options['env']['CM_ENABLE_LIVE_UI'], '0')
        self.assertEqual(options['env']['CM_ENABLE_SETTINGS_UI'], '1')
        self.assertEqual(options['env']['CM_ENABLE_LOGGING'], '0')

    def test_wrong_registry_owner_is_rejected_before_ui_build(self):
        (self.library / '.piopm').write_text(json.dumps({'version':'4.9.0','spec':{'owner':'other'}}))
        with self.assertRaisesRegex(RuntimeError, 'registry'):
            self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_explicit_local_development_package_is_version_checked(self):
        self.prepare_development()
        self.run_hook()
        self.assertEqual(len(self.calls), 1)

    def test_real_scons_context_without_file_resolves_helper_from_project_dir(self):
        self.prepare_development()
        self.run_hook(without_file=True)
        self.assertEqual(len(self.calls), 1)

    def prepare_development(self):
        self.environment.development = True
        (self.library / "library.json").write_text(json.dumps({"name": "ESP32 Configuration Manager", "version": "4.10.0"}))
        (self.library / "src/ConfigManager.h").write_text('#define CONFIGMANAGER_VERSION "4.10.0"')
        (self.library / "webui/package.json").write_text(json.dumps({"version": "4.10.0"}))
        (self.library / ".piopm").write_text(json.dumps({"version": "4.10.0", "spec": {"uri": DEVELOPMENT_URI}}))
        for name in ("src/wifi/WiFiManager.cpp", "src/core/CoreSettings.h", "src/mqtt/MQTTManager.h", "src/html_content.cpp", "webui/dist/index.html"):
            path = self.library / name
            path.parent.mkdir(exist_ok=True, parents=True)
            path.write_text("// [MOCKED!] Immutable input or initial generated asset\n")
        self.package = self.root / ".local/cm-wifi-sleep-4.10.0-package"
        shutil.copytree(self.library, self.package, ignore=shutil.ignore_patterns('.piopm'))
        manifest = "# source_head=" + "a" * 40 + "\n# source_version=4.10.0\n"
        manifest += "".join(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.relative_to(self.package).as_posix() + "\n"
                            for path in sorted(self.package.rglob('*')) if path.is_file())
        (self.package / "manifest.sha").write_text(manifest)
        (self.library / "manifest.sha").write_text(manifest)

    def test_stale_consumed_wifi_or_core_source_is_rejected(self):
        self.prepare_development()
        for name in ("src/wifi/WiFiManager.cpp", "src/core/CoreSettings.h", "src/html_content.cpp"):
            with self.subTest(name=name):
                path = self.library / name
                original = path.read_bytes()
                path.write_text("// [MOCKED!] Stale consumer source\n")
                with self.assertRaisesRegex(RuntimeError, "stale|hash|manifest"):
                    self.run_hook()
                path.write_bytes(original)
        self.assertEqual(len(self.calls), 0)

    def test_changed_immutable_package_is_rejected(self):
        self.prepare_development()
        (self.package / "src/core/CoreSettings.h").write_text("// [MOCKED!] Changed frozen source\n")
        with self.assertRaisesRegex(RuntimeError, "hash|manifest|package"):
            self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_extra_productive_source_is_rejected(self):
        self.prepare_development()
        (self.library / "src/wifi/Unexpected.cpp").write_text("// [MOCKED!] Unmanifested source\n")
        with self.assertRaisesRegex(RuntimeError, "manifest|source"):
            self.run_hook()

    def test_generated_ui_can_change_without_mutating_frozen_inputs(self):
        self.prepare_development()
        original = (self.package / "src/html_content.h").read_bytes()
        def regenerate(*args, **kwargs):
            self.builder(*args, **kwargs)
            (self.library / "webui/dist/index.html").write_text("[MOCKED!] Regenerated Settings-only bundle")
        self.run_hook(builder=regenerate)
        self.run_hook(builder=regenerate)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual((self.package / "src/html_content.h").read_bytes(), original)

    def test_generator_cannot_modify_non_ui_sources(self):
        self.prepare_development()
        def corrupt(*args, **kwargs):
            self.builder(*args, **kwargs)
            (self.library / "src/core/CoreSettings.h").write_text("// [MOCKED!] Unexpected generator mutation\n")
        with self.assertRaisesRegex(RuntimeError, "stale|hash|manifest"):
            self.run_hook(builder=corrupt)

    def test_development_provenance_must_match_selected_uri(self):
        self.prepare_development()
        (self.library / ".piopm").write_text(json.dumps({"version": "4.10.0", "spec": {"uri": "file://../ConfigurationsManager"}}))
        with self.assertRaisesRegex(RuntimeError, "provenance"):
            self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_missing_or_unsafe_source_manifest_is_rejected(self):
        self.prepare_development()
        manifest = self.package / 'manifest.sha'
        original = manifest.read_text()
        manifest.unlink()
        with self.assertRaisesRegex(RuntimeError, "manifest"):
            self.run_hook()
        for name in ("../escape.cpp", "C:/escape.cpp", "src\\escape.cpp"):
            manifest.write_text(original + 'a'*64 + '  ' + name + '\n')
            with self.assertRaisesRegex(RuntimeError, "manifest"):
                self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_package_helper_preserves_a_frozen_destination(self):
        self.prepare_development()
        spec = importlib.util.spec_from_file_location('package_helper_test', ROOT / 'tools/prepare_cm_development.py')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        destination = self.root / '.local/fresh-freeze'
        with patch.object(helper.subprocess, 'check_output', return_value='b'*40 + '\n'):
            helper.prepare(self.package, destination)
            original = (destination / 'manifest.sha').read_bytes()
            helper.prepare(self.package, destination)
            self.assertEqual((destination / 'manifest.sha').read_bytes(), original)
            (self.package / 'src/core/CoreSettings.h').write_text('// [MOCKED!] Later development change\n')
            with self.assertRaisesRegex(RuntimeError, 'choose a new'):
                helper.prepare(self.package, destination)
        self.assertEqual((destination / 'manifest.sha').read_bytes(), original)

    def test_local_development_wrong_version_is_rejected(self):
        self.environment.development = True
        with self.assertRaisesRegex(RuntimeError, 'manifest|version'):
            self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_compiler_live_profile_is_rejected_before_ui_build(self):
        self.environment['BUILD_FLAGS'] = '-DCM_ENABLE_LIVE_UI=1 -DCM_ENABLE_SETTINGS_UI=1'
        with self.assertRaisesRegex(RuntimeError, 'Settings-only'):
            self.run_hook()
        self.assertEqual(len(self.calls), 0)

    def test_valid_flavor_with_stale_asset_hash_fails_closed(self):
        def stale_builder(*args, **kwargs):
            self.builder(*args, **kwargs)
            with self.asset.open('a') as output:
                output.write('// stale asset after build\n')
        with self.assertRaisesRegex(RuntimeError, 'hash'):
            self.run_hook(builder=stale_builder)

    def test_builder_leaving_wrong_asset_flavor_fails_closed(self):
        with self.assertRaisesRegex(RuntimeError, 'flavor|flags|WebUI'):
            self.run_hook(builder=lambda *args, **kwargs: None)


if __name__ == '__main__':
    unittest.main()
