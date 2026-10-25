"""Consumer build checks; [MOCKED!] SCons and the external CM UI subprocess only."""
import hashlib
import json
from pathlib import Path
import runpy
import shlex
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/precompile_wrapper.py"


class BuildEnvironment(dict):
    def __init__(self, root):
        super().__init__(PIOENV="usb", BUILD_FLAGS="-DCM_ENABLE_LIVE_UI=0 -DCM_ENABLE_SETTINGS_UI=1 -DCM_ENABLE_LOGGING=0")
        self.root = root
        self.development = False

    def subst(self, key):
        return str(self.root / ".pio/libdeps") if key == "$PROJECT_LIBDEPS_DIR" else str(self.root)

    def GetProjectOption(self, key, default=None):
        if key == "lib_deps":
            return ["file://../ConfigurationsManager"] if self.development else ["vitaly.ruhl/ESP32 Configuration Manager@4.9.0"]
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

    def run_hook(self, builder=None):
        script_module = types.ModuleType("SCons.Script")
        def inject(name):
            sys._getframe(1).f_globals[name] = self.environment
        script_module.Import = inject
        with patch.dict(sys.modules, {"SCons": types.ModuleType("SCons"), "SCons.Script": script_module}), patch("subprocess.run", side_effect=builder or self.builder):
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
        self.assertEqual(self.calls, [])

    def test_explicit_local_development_package_is_version_checked(self):
        self.environment.development = True
        (self.library / "library.json").write_text(json.dumps({"name": "ESP32 Configuration Manager", "version": "4.9.1"}))
        (self.library / "src/ConfigManager.h").write_text('#define CONFIGMANAGER_VERSION "4.9.1"')
        (self.library / "webui/package.json").write_text(json.dumps({"version": "4.9.1"}))
        self.run_hook()
        self.assertEqual(len(self.calls), 1)

    def test_local_development_wrong_version_is_rejected(self):
        self.environment.development = True
        with self.assertRaisesRegex(RuntimeError, 'manifest|version'):
            self.run_hook()
        self.assertEqual(self.calls, [])

    def test_compiler_live_profile_is_rejected_before_ui_build(self):
        self.environment['BUILD_FLAGS'] = '-DCM_ENABLE_LIVE_UI=1 -DCM_ENABLE_SETTINGS_UI=1'
        with self.assertRaisesRegex(RuntimeError, 'Settings-only'):
            self.run_hook()
        self.assertEqual(self.calls, [])

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
