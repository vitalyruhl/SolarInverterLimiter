#!/usr/bin/env python3
"""Build the WebUI from the exact PlatformIO-resolved CM dependency."""

import os
from pathlib import Path
import subprocess
import sys

from SCons.Script import Import
Import("env")

# Use the compiler-aware archiver for size-saving link-time optimization.
env.Replace(AR="xtensa-esp32-elf-gcc-ar", RANLIB="xtensa-esp32-elf-gcc-ranlib")

project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")).resolve()
library_dir = libdeps_dir / env["PIOENV"] / "ESP32 Configuration Manager"
precompile_script = library_dir / "tools" / "preCompile_script.py"
if not precompile_script.is_file():
    raise RuntimeError(f"CM precompile script missing: {precompile_script}")

# Do not overwrite the pinned dependency from an unrelated local checkout.
process_env = os.environ.copy()
process_env["NODE_ENV"] = "development"
subprocess.run(
    [sys.executable, str(precompile_script)],
    cwd=library_dir,
    env=process_env,
    check=True,
)
