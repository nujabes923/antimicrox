"""Run Windows offline regressions using a completed MSVC/Ninja application build.

Usage: python tests/run_offline_controls.py [build-random-turbo]
The recording event handler never sends input to the desktop. A timeout detects
the same-thread deadlocks that motivated these tests.
"""
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = (root / (sys.argv[1] if len(sys.argv) > 1 else "build-random-turbo")).resolve()
ninja = (build / "build.ninja").read_text(encoding="utf-8")
cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")

def field(name):
    return re.search(r"^  " + name + r" = (.*)$", ninja, re.M).group(1)

compiler = Path(re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.*)$", cache, re.M).group(1))
vcvars = next(parent / "Auxiliary/Build/vcvars64.bat" for parent in compiler.parents
              if (parent / "Auxiliary/Build/vcvars64.bat").exists())
obj = build / "offlinecontrols.obj"
exe = build / "bin/offlinecontrols.exe"
compile_rsp = build / "offlinecontrols-compile.rsp"
compile_rsp.write_text("\n".join([
    field("DEFINES"), field("FLAGS"), field("INCLUDES"),
    f'/c "{root / "tests/offlinecontrols.cpp"}" /Fo"{obj}"',
]), encoding="utf-8")
objects = [p for p in (build / "CMakeFiles/antimicrox.dir").rglob("*.obj") if p.name != "main.cpp.obj"]
assert objects, "Build the application first"
link_rsp = build / "offlinecontrols-link.rsp"
link_rsp.write_text("\n".join([
    '/nologo /subsystem:console', f'/out:"{exe}"', f'"{obj}"',
    *[f'"{p}"' for p in objects], field("LINK_LIBRARIES"),
]), encoding="utf-8")
script = build / "offlinecontrols-build.cmd"
script.write_text("\n".join([
    '@echo off', f'call "{vcvars}" >nul', 'if errorlevel 1 exit /b 1',
    f'cl.exe @"{compile_rsp}"', 'if errorlevel 1 exit /b 1',
    f'link.exe @"{link_rsp}"',
]), encoding="utf-8")
subprocess.run([str(script)], cwd=build, check=True)
qt_dir = Path(re.search(r"^Qt6_DIR:PATH=(.*)$", cache, re.M).group(1)).parents[2]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORM_PLUGIN_PATH=str(qt_dir / "plugins/platforms"),
           ANTIMICROX_TEST_ARTIFACT_DIR=str(build))
subprocess.run([str(exe)], cwd=root, env=env, check=True, timeout=30)
if "--native-hotkey" in sys.argv[2:]:
    env["QT_QPA_PLATFORM"] = "windows"
    subprocess.run([str(exe), "--native-hotkey"], cwd=root, env=env, check=True, timeout=30)
