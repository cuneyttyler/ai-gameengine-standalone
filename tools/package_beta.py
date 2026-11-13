"""The Beta Release package (docs/beta-release.md): the built editor with everything it reads beside it, and the
AI service without any API keys. Each tester sets their own in Preferences > Beta Settings.

    python tools/package_beta.py [--days 30] [--out dist] [--build build] [--config Release] [--zip]

The editor is built for it with its last day (today + --days, AIGE_BETA_EXPIRES), and afterwards built again without
one (your own build never expires).

Makes <out>/AIGE-Beta-<date>/:
    editor.exe, shaders/        the editor (build/apps/editor/<config>)
    ui/                         its interface (apps/editor/ui/dist)
    player/                     the game player for File > Export (build/apps/sandbox/<config>)
    data/                       the engine's data: places, story, fx, magic, ui, planner, help
    ai_service/                 the AI service's code, pyproject.toml and uv.lock: no .env, no .venv, no caches
    python/                     its own Python (a standalone CPython) with every package the service uses installed,
                                as uv.lock pins them: the editor runs the service on it, so testers need nothing else
    AIGE-User-Guide.pdf         the User Guide, where a tester sees it first
    docs/                       the manuals
    README-BETA.txt

Then every file of it is searched for the keys in ai_service/.env (and for anything that looks like a key); one
found stops the packaging, and the package is removed.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# The AI service: what it runs from. Never .env (our keys), .venv (uv makes it on the first start), caches, tests.
SERVICE_SKIP = {".env", ".venv", ".cache", ".pytest_cache", "__pycache__", "tests", ".ruff_cache", ".mypy_cache"}
SERVICE_SKIP_SUFFIX = (".pyc", ".log", ".sqlite", ".env")
# Anything that looks like a key: a provider's prefix and a long token.
KEY_PATTERNS = [re.compile(p) for p in (rb"sk-ant-[A-Za-z0-9_\-]{20,}", rb"sk-or-v1-[A-Za-z0-9]{20,}", rb"\bsk-[A-Za-z0-9]{32,}")]

README = """AIGE Beta
=========

What you need
- Windows 10 or 11 (64-bit), with the Microsoft Edge WebView2 Runtime (installed with Windows 11 and recent Windows 10).
- Your own API keys. The beta comes with none: each provider is used, and paid for, with your own account.
Nothing else: the AI service comes with its own Python, ready (python/).

Starting
1. Run editor.exe. Make a project (File > New Project) or open one.
2. Open File > Preferences > Beta Settings. Set a key for a language model (Mistral, Anthropic or OpenRouter),
   and for the art you want: Black Forest Labs (FLUX) paints places and backgrounds, PixelLab draws pixel art and
   animates things, AutoSprite animates characters. "Models in use" shows each job's provider and model, and
   whether its key is set; Change takes you to where it's chosen.
3. Say yes to AI. The AI service starts on the package's own Python (a few seconds; click-to-select's model is
   fetched the first time it's used).

This beta build works until __EXPIRES__. After that it says so when started: download the new one from
aige-dev.com/beta (your projects and keys are kept).

Your keys are kept on this computer, encrypted for your Windows user (%LOCALAPPDATA%\\aige\\api-keys.json), and sent
only to the provider each is for.

Start with AIGE-User-Guide.pdf, next to editor.exe. The other manuals (the Builder, the AI Planner, the guide as
Word and slides) are in docs/ (Help > Documentation opens it), and online at aige-dev.com/docs.
"""


def copy_tree(src: Path, dst: Path, skip_dirs: set[str] = frozenset(), skip_suffix: tuple[str, ...] = ()) -> int:
    n = 0
    for path in src.rglob("*"):
        rel = path.relative_to(src)
        if any(part in skip_dirs for part in rel.parts):
            continue
        if path.is_dir() or path.name.endswith(skip_suffix) or path.name in skip_dirs:
            continue
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        n += 1
    return n


PYTHON_VERSION = "3.12"


def run(cmd: list[str], cwd: Path | None = None) -> str:
    done = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != 0:
        raise SystemExit(f"Failed: {' '.join(cmd)}\n{done.stdout[-2000:]}\n{done.stderr[-2000:]}")
    return done.stdout


def bundle_python(package: Path, cache: Path) -> int:
    """A standalone CPython (relocatable: it runs from any folder) at package/python, and the service's packages
    installed into it, exactly as uv.lock pins them (the segment extra too: what the editor's own launch uses)."""
    cache.mkdir(parents=True, exist_ok=True)
    run(["uv", "python", "install", PYTHON_VERSION, "--install-dir", str(cache), "--no-bin"])
    found = sorted(d for d in cache.iterdir() if d.is_dir() and not d.is_symlink() and d.name.startswith(f"cpython-{PYTHON_VERSION}.")
                   and (d / "python.exe").exists())
    if not found:
        raise SystemExit(f"No standalone Python {PYTHON_VERSION} in {cache}")
    python_dir = package / "python"
    shutil.copytree(found[-1], python_dir, symlinks=False)
    (python_dir / "Lib" / "EXTERNALLY-MANAGED").unlink(missing_ok=True)  # it's ours: packages go into it
    python = python_dir / "python.exe"
    with tempfile.TemporaryDirectory() as tmp:
        requirements = Path(tmp) / "requirements.txt"
        run(["uv", "export", "--frozen", "--no-dev", "--extra", "segment", "--no-hashes", "--no-emit-project", "-o", str(requirements)],
            cwd=ROOT / "ai_service")
        # Copies, not links into uv's cache: the package is moved to other machines.
        run(["uv", "pip", "install", "--python", str(python), "--link-mode", "copy", "-r", str(requirements)])
    # It starts: the service's code imports on it (cwd ai_service, as the editor runs it), with no key and no .env.
    env = {k: v for k, v in os.environ.items() if not k.endswith(("_API_KEY", "_AUTH_TOKEN"))}
    env.update({"PYTHONNOUSERSITE": "1", "PYTHONUTF8": "1"})
    check = subprocess.run([str(python), "-c", "import app.main, uvicorn, torch, transformers; print('ok')"], cwd=package / "ai_service",
                           capture_output=True, text=True, env=env)
    if check.returncode != 0 or "ok" not in check.stdout:
        raise SystemExit(f"The bundled Python can't run the service:\n{check.stderr[-3000:]}")
    # What that start left behind (the help's search index, the code's bytecode): each tester's service makes its own.
    shutil.rmtree(package / "ai_service" / ".cache", ignore_errors=True)
    for cache in (package / "ai_service").rglob("__pycache__"):
        shutil.rmtree(cache, ignore_errors=True)
    # Every module the service's code imports, those imported inside functions too (OpenRouter's httpx was a dev
    # dependency only, and the package lacked it): each must be there.
    names = set()
    for f in (package / "ai_service" / "app").rglob("*.py"):
        for line in f.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*(?:from\s+([A-Za-z_][\w.]*)\s+import|import\s+([A-Za-z_][\w.]*))", line)
            if m and (m.group(1) or m.group(2)).split(".")[0] != "app":
                names.add((m.group(1) or m.group(2)).split(".")[0])
    probe = "import importlib.util, sys; print(' '.join(m for m in sys.argv[1:] if importlib.util.find_spec(m) is None))"
    missing = subprocess.run([str(python), "-c", probe, *sorted(names)], capture_output=True, text=True, env=env).stdout.split()
    if missing:
        raise SystemExit(f"The service imports what the bundled Python hasn't: {', '.join(missing)} (add it to "
                         "ai_service/pyproject.toml's dependencies, then uv lock)")
    return sum(1 for f in python_dir.rglob("*") if f.is_file())


def our_keys() -> list[bytes]:
    """The values in ai_service/.env: none of them may be in the package."""
    env = ROOT / "ai_service" / ".env"
    keys = []
    if env.exists():
        for line in env.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                value = line.split("=", 1)[1].strip().strip("'\"")
                if len(value) >= 12:
                    keys.append(value.encode())
    return keys


def scan(package: Path) -> list[str]:
    keys = our_keys()
    found = []
    for path in package.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        rel = path.relative_to(package).as_posix()
        if path.name == ".env" or (path.name.endswith(".env") and not rel.startswith("python/")):
            found.append(f"{rel}: an .env file")
        for k in keys:
            if k in data or k.decode(errors="ignore").encode("utf-16-le") in data:
                found.append(f"{rel}: one of ai_service/.env's keys")
        ours = not rel.startswith("python/")
        if ours and path.suffix.lower() in (".py", ".json", ".md", ".txt", ".toml", ".js", ".html", ".css", ".lock"):
            for p in KEY_PATTERNS:
                if p.search(data):
                    found.append(f"{rel}: something that looks like a key ({p.pattern.decode()[:12]}...)")
    return found


def build_editor(a, expires: str) -> None:
    """The editor and the player built with AIGE_BETA_EXPIRES (apps/editor/BetaExpiry.cpp; only that file recompiles)."""
    print(f"Building the editor {'for a beta ending ' + expires if expires else 'as your own again (no end)'}...")
    run(["cmake", "-S", str(ROOT), "-B", str(ROOT / a.build), f"-DAIGE_BETA_EXPIRES={expires}"])
    run(["cmake", "--build", str(ROOT / a.build), "--config", a.config, "--target", "editor", "sandbox"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="dist")
    ap.add_argument("--build", default="build")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--zip", action="store_true", help="also make <package>.zip")
    ap.add_argument("--no-python", action="store_true", help="leave out the bundled Python (testers then need uv)")
    ap.add_argument("--days", type=int, default=30, help="how long the beta works, from today (default 30)")
    a = ap.parse_args()
    expires = (dt.date.today() + dt.timedelta(days=a.days)).isoformat()
    build_editor(a, expires)
    try:
        return make_package(a, expires)
    finally:
        build_editor(a, "")  # your own editor again: it never expires


def make_package(a, expires: str) -> int:
    build = ROOT / a.build
    editor = build / "apps" / "editor" / a.config
    player = build / "apps" / "sandbox" / a.config
    ui = ROOT / "apps" / "editor" / "ui" / "dist"
    for need, what in ((editor / "editor.exe", "the built editor"), (player / "sandbox.exe", "the built player"), (ui / "index.html", "the built UI")):
        if not need.exists():
            print(f"Missing {what}: {need}. Build it first (cmake --build {a.build} --config {a.config} --target editor sandbox).")
            return 1

    name = f"AIGE-Beta-{dt.date.today().isoformat()}"
    package = ROOT / a.out / name
    # An earlier package that's running (its AI service holds files in it) isn't removed: this one gets the next name.
    for n in range(2, 50):
        if not package.exists():
            break
        # Moved aside first: Windows refuses to rename a folder with a file open in it, so one in use is left whole
        # (removing it outright deletes what it can before it meets the open file).
        aside = package.with_name(package.name + ".old")
        try:
            if aside.exists():
                shutil.rmtree(aside)
            package.rename(aside)
            shutil.rmtree(aside)
        except PermissionError:
            print(f"{package.name} is in use (an editor running from it?): making {name}-{n} instead")
            package = ROOT / a.out / f"{name}-{n}"
    package.mkdir(parents=True)

    counts = {}
    # The editor: the exe and its shaders (not its WebView2 profile, which is the user's).
    for f in editor.iterdir():
        if f.is_file() and f.suffix.lower() in (".exe", ".dll"):
            shutil.copy2(f, package / f.name)
    counts["editor"] = copy_tree(editor / "shaders", package / "shaders") + 1
    counts["ui"] = copy_tree(ui, package / "ui")
    counts["player"] = copy_tree(player, package / "player", skip_suffix=(".pdb", ".ilk"))
    counts["data"] = copy_tree(ROOT / "data", package / "data")
    counts["ai_service"] = copy_tree(ROOT / "ai_service", package / "ai_service", SERVICE_SKIP, SERVICE_SKIP_SUFFIX)
    counts["docs"] = copy_tree(ROOT / "docs" / "manuals", package / "docs")
    # The User Guide where a tester sees it first: beside editor.exe.
    if (ROOT / "docs" / "manuals" / "AIGE-User-Guide.pdf").exists():
        shutil.copy2(ROOT / "docs" / "manuals" / "AIGE-User-Guide.pdf", package / "AIGE-User-Guide.pdf")
    if not a.no_python:
        print("Bundling Python and the service's packages (torch is large: a few minutes)...")
        counts["python"] = bundle_python(package, ROOT / a.out / ".python-cache")

    # Its last day is in the packaged editor (else it would never expire).
    if f"AIGE-BETA-EXPIRES={expires};".encode() not in (package / "editor.exe").read_bytes():
        shutil.rmtree(package)
        print(f"Not packaged: editor.exe wasn't built with the beta's last day ({expires}).")
        return 3
    (package / "README-BETA.txt").write_text(README.replace("__EXPIRES__", expires), encoding="utf-8")
    problems = scan(package)
    if problems:
        print("Not packaged: keys found in it:")
        for p in problems:
            print("  " + p)
        shutil.rmtree(package)
        return 2

    size = sum(f.stat().st_size for f in package.rglob("*") if f.is_file())
    print(f"{package}  ({size / 1e6:.1f} MB)")
    for k, v in counts.items():
        print(f"  {k}: {v} files")
    print("  no keys in it: ai_service/.env left out, every file searched")
    if a.zip:
        archive = shutil.make_archive(str(package), "zip", package.parent, package.name)
        print(f"  {archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
