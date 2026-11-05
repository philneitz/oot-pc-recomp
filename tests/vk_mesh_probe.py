#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile original task/mesh fixtures and exercise the Vulkan pipeline on a real GPU; no game data."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    args=parser.parse_args()
    compiler=shutil.which("glslangValidator")
    validator=shutil.which("spirv-val")
    if not compiler or not validator:
        parser.error("glslangValidator and spirv-val must be on PATH")
    output=ROOT/"build/test/mesh-probe"
    output.mkdir(parents=True,exist_ok=True)
    for source,dest,defines in (("mesh_triangle.mesh","mesh.spv",[]),
                                ("mesh_triangle.mesh","mesh-task.spv",["-DWITH_TASK=1"]),
                                ("mesh_triangle.task","task.spv",[]),("mesh_triangle.frag","fragment.spv",[])):
        subprocess.run([compiler,"-V","--target-env","vulkan1.3",*defines,
                        str(ROOT/"tests/fixtures"/source),"-o",str(output/dest)],check=True)
        subprocess.run([validator,"--target-env","vulkan1.3",str(output/dest)],check=True)
    env=dict(os.environ,OOT_MESH_PROBE_DIR=str(output),TOTK_VK_VALIDATION="1",TOTK_GPU_NAME="NVIDIA",OOT_MESH_SHADER="1")
    return subprocess.run([str(args.exe.resolve()),"--probe-vkmesh"],cwd=ROOT,env=env).returncode

if __name__=="__main__":
    raise SystemExit(main())
