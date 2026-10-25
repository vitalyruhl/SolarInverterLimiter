"""Create a bounded local CM development package without modifying its checkout."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess


EXCLUDED = {"node_modules", ".pio", ".git", ".local", ".temp", "__pycache__", ".cache"}
ROOT_FILES = ("library.json", "README.md", "README", "README.txt", "LICENSE", "LICENSE.md", "LICENSE.txt")
TREE_ROOTS = ("src", "tools", "webui")
MANIFEST = "manifest.sha"


def is_reparse(path: Path) -> bool:
    return path.is_symlink() or (
        path.exists()
        and bool(getattr(path.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)
    )


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def files_under(root: Path, relative: Path = Path(), *, package_check: bool = False) -> list[Path]:
    """Walk only an explicitly selected tree and never follow symbolic links."""
    result = []
    with os.scandir(root / relative) as entries:
        for entry in sorted(entries, key=lambda item: item.name):
            child = relative / entry.name
            if entry.is_symlink() or getattr(entry.stat(follow_symlinks=False), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                if package_check:
                    raise RuntimeError("Existing package contains a symbolic link or junction")
                continue
            if entry.is_dir(follow_symlinks=False):
                if not package_check and entry.name.casefold() in EXCLUDED:
                    continue
                if not package_check and child.as_posix().casefold() == "tools/_buildlogs":
                    continue
                result.extend(files_under(root, child, package_check=package_check))
            elif entry.is_file(follow_symlinks=False):
                result.append(child)
    return result


def source_files(source: Path) -> list[Path]:
    selected = []
    for name in ROOT_FILES:
        candidate = source / name
        if candidate.is_file() and not is_reparse(candidate):
            selected.append(Path(name))
    for name in TREE_ROOTS:
        candidate = source / name
        if is_reparse(candidate):
            raise RuntimeError(f"Refusing symbolic-link source tree: {name}")
        if not candidate.is_dir():
            raise RuntimeError(f"Required source tree missing: {name}")
        selected.extend(files_under(source, Path(name)))
    if Path("library.json") not in selected:
        raise RuntimeError("Required library.json missing")
    return sorted(selected, key=lambda item: item.as_posix())


def prepare(source: Path, destination: Path) -> tuple[int, int, str, str]:
    if is_reparse(source):
        raise RuntimeError("Refusing symbolic-link source checkout")
    selected = source_files(source)
    metadata = json.loads((source / "library.json").read_text(encoding="utf-8"))
    if metadata.get("version") != "4.9.1":
        raise RuntimeError("Expected CM development version 4.9.1")
    head = subprocess.check_output(
        ["git", "-c", f"safe.directory={source.as_posix()}", "-C", str(source), "rev-parse", "HEAD"],
        text=True,
    ).strip()
    hashes = {relative.as_posix(): digest(source / relative) for relative in selected}
    mqtt = [f"{value}  {name}\n" for name, value in hashes.items() if name.startswith("src/mqtt/") and name.endswith(".h")]
    if not mqtt:
        raise RuntimeError("CM MQTT headers missing")
    manifest = f"# source_head={head}\n# source_version=4.9.1\n" + "".join(
        f"{value}  {name}\n" for name, value in hashes.items()
    )
    mqtt_hash = hashlib.sha256("".join(mqtt).encode("utf-8")).hexdigest()

    if is_reparse(destination) or is_reparse(destination.parent):
        raise RuntimeError("Refusing symbolic-link destination")
    if destination.exists():
        if not destination.is_dir() or not (destination / MANIFEST).is_file():
            raise RuntimeError("Existing package is incomplete; choose a new reviewed target")
        actual = {relative.as_posix() for relative in files_under(destination, package_check=True)}
        expected = set(hashes) | {MANIFEST}
        if actual != expected or (destination / MANIFEST).read_text(encoding="utf-8") != manifest:
            raise RuntimeError("Existing package differs from current CM sources; choose a new reviewed target")
        expected_dirs = {parent.as_posix() for relative in selected for parent in relative.parents if parent != Path()}
        actual_dirs = {
            (Path(parent) / child).relative_to(destination).as_posix()
            for parent, children, _ in os.walk(destination, followlinks=False)
            for child in children
        }
        if actual_dirs != expected_dirs:
            raise RuntimeError("Existing package directory tree differs; choose a new reviewed target")
        if any(digest(destination / name) != value for name, value in hashes.items()):
            raise RuntimeError("Existing package files changed; choose a new reviewed target")
    else:
        destination.mkdir(parents=True, exist_ok=False)
        for relative in selected:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target, follow_symlinks=False)
            if target.is_symlink() or digest(target) != hashes[relative.as_posix()]:
                raise RuntimeError(f"Source changed during package preparation: {relative.as_posix()}")
        (destination / MANIFEST).write_text(manifest, encoding="utf-8", newline="\n")
    total = sum((destination / relative).stat().st_size for relative in selected)
    return len(selected), total, head, mqtt_hash


def main() -> None:
    repository = Path(__file__).resolve().parent.parent
    source = repository.parent / "ConfigurationsManager"
    destination = repository / ".local" / "cm-hardening-package"
    count, total, head, mqtt_hash = prepare(source, destination)
    print(f"[I] CM package ready files={count} bytes={total} source_head={head} mqtt_sha256={mqtt_hash}")


if __name__ == "__main__":
    main()
