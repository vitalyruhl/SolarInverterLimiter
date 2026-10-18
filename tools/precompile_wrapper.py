#!/usr/bin/env python3
"""Validate the selected CM dependency without modifying its source or WebUI."""

import json
import re
from pathlib import Path

from SCons.Script import Import
Import("env")

# Use the compiler-aware archiver for size-saving link-time optimization.
env.Replace(AR="xtensa-esp32-elf-gcc-ar", RANLIB="xtensa-esp32-elf-gcc-ranlib")

project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")).resolve()
# A development symlink resolves to the adjacent checkout, including local edits.
# Resolve that declared dependency rather than assuming a package-directory name.
dependencies = env.GetProjectOption("lib_deps", [])
if isinstance(dependencies, str):
    dependencies = dependencies.splitlines()
links = [value.strip()[len("symlink://"):] for value in dependencies
         if value.strip().startswith("symlink://")]
if len(links) > 1:
    raise RuntimeError("Select exactly one local ConfigManager dependency.")
library_dir = ((project_dir / links[0]).resolve() if links else
               libdeps_dir / env["PIOENV"] / "ESP32 Configuration Manager")
manifest = library_dir / "library.json"
if not manifest.is_file():
    raise RuntimeError(f"ConfigManager manifest missing: {manifest}")
if manifest.is_file():
    metadata = json.loads(manifest.read_text(encoding="utf-8"))
    if metadata.get("name") != "ESP32 Configuration Manager":
        raise RuntimeError("The local dependency is not ConfigManager.")
    version = metadata.get("version", "")
    try:
        compatible = tuple(map(int, version.split("."))) >= (4, 7, 0)
    except ValueError:
        compatible = False
    if not compatible:
        raise RuntimeError("SIL compact metadata requires CM >=4.7.0; select a compatible adjacent ConfigurationsManager checkout.")
header = (library_dir / "src" / "ConfigManager.h").read_text(encoding="utf-8")
match = re.search(r'^#define\s+CONFIGMANAGER_VERSION\s+"([^"]+)"', header, re.MULTILINE)
web_version = json.loads((library_dir / "webui" / "package.json").read_text(encoding="utf-8")).get("version")
if not match or match.group(1) != version or web_version != version:
    raise RuntimeError("ConfigManager version mirrors disagree; select one coherent checkout.")
asset = library_dir / "src" / "html_content.h"
if not asset.is_file() or not asset.stat().st_size:
    raise RuntimeError("ConfigManager WebUI asset missing; prepare the selected checkout separately.")
# Consumer builds use the reviewed asset and never regenerate shared CM files.
print(f"[I] Using ConfigManager {version} from {library_dir}; existing WebUI ready")
