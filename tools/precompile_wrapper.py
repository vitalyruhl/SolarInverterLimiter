#!/usr/bin/env python3
"""Build and verify the pinned registry CM WebUI inside this consumer's libdeps."""

import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from SCons.Script import Import
Import("env")

# Use the compiler-aware archiver for size-saving link-time optimization.
env.Replace(AR="xtensa-esp32-elf-gcc-ar", RANLIB="xtensa-esp32-elf-gcc-ranlib")

requirement = "vitaly.ruhl/ESP32 Configuration Manager@4.9.0"
dependencies = env.GetProjectOption("lib_deps", [])
if isinstance(dependencies, str):
    dependencies = dependencies.splitlines()
if [value.strip() for value in dependencies].count(requirement) != 1:
    raise RuntimeError(f"Select the pinned registry dependency: {requirement}")

libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")).resolve() / env["PIOENV"]
library_dir = (libdeps_dir / "ESP32 Configuration Manager").resolve()
if not library_dir.is_relative_to(libdeps_dir.resolve()):
    raise RuntimeError("ConfigManager must be a consumer-local registry package, not a source symlink.")
try:
    provenance = json.loads((library_dir / ".piopm").read_text(encoding="utf-8"))
except (OSError, ValueError) as error:
    raise RuntimeError("ConfigManager registry provenance missing or invalid.") from error
if provenance.get("version") != "4.9.0" or provenance.get("spec", {}).get("owner") != "vitaly.ruhl":
    raise RuntimeError("ConfigManager registry owner/version does not match the pinned requirement.")
metadata = json.loads((library_dir / "library.json").read_text(encoding="utf-8"))
if metadata.get("name") != "ESP32 Configuration Manager" or metadata.get("version") != "4.9.0":
    raise RuntimeError("ConfigManager registry manifest does not match the pinned requirement.")
header = (library_dir / "src/ConfigManager.h").read_text(encoding="utf-8")
match = re.search(r'^#define\s+CONFIGMANAGER_VERSION\s+"([^\"]+)"', header, re.MULTILINE)
web_version = json.loads((library_dir / "webui/package.json").read_text(encoding="utf-8")).get("version")
if not match or match.group(1) != "4.9.0" or web_version != "4.9.0":
    raise RuntimeError("ConfigManager registry version mirrors disagree.")

# Reuse the published flag resolver and UI builder; never use the adjacent checkout
# or modify PlatformIO's shared downloaded-package cache.
resolver = importlib.util.spec_from_file_location("sil_cm_webui_features", library_dir / "tools/webui_features.py")
features_module = importlib.util.module_from_spec(resolver)
resolver.loader.exec_module(features_module)
features = features_module.feature_values(env)
if (features["CM_ENABLE_LIVE_UI"], features["CM_ENABLE_SETTINGS_UI"], features["CM_EMBED_WEBUI"]) != ("0", "1", "1"):
    raise RuntimeError("SIL requires embedded Settings-only WebUI: Live=0, Settings=1.")
process_env = os.environ.copy()
process_env.update(features)
process_env.update({"VITE_" + key: value for key, value in features.items()})
process_env.update(NODE_ENV="production", NPM_CONFIG_INCLUDE="dev")
subprocess.run([sys.executable, str(library_dir / "tools/preCompile_script.py")],
               cwd=str(library_dir), env=process_env, check=True)

asset = library_dir / "src/html_content.h"
asset_text = asset.read_text(encoding="utf-8")
for macro, expected in (("CM_WEBUI_LIVE_UI", "0"), ("CM_WEBUI_SETTINGS_UI", "1")):
    if not re.search(r"^#define\s+" + macro + r"\s+" + expected + r"\s*$", asset_text, re.MULTILINE):
        raise RuntimeError("Generated ConfigManager WebUI flavor does not match compiler flags.")
try:
    cached = json.loads((library_dir / ".pio/cm-webui-build.json").read_text(encoding="utf-8"))
except (OSError, ValueError) as error:
    raise RuntimeError("Generated ConfigManager WebUI source/flags proof missing.") from error
fingerprint = features_module.source_fingerprint(library_dir, {**features, "NODE_ENV": "production"})
asset_sha256 = hashlib.sha256(asset.read_bytes()).hexdigest()
if (cached.get("fingerprint") != fingerprint or cached.get("features") != features or
        cached.get("header_sha256") != asset_sha256):
    raise RuntimeError("Generated ConfigManager WebUI source, flags or asset hash disagree.")
print(f"[I] Registry CM 4.9.0; WebUI Live=0 Settings=1; asset={asset_sha256}")
