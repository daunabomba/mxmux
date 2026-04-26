import subprocess
import os
import multiprocessing
import shutil
from pathlib import Path
from mods import colors

def get_env():
    env = os.environ.copy()
    # Path to our host-built LLVM tools
    project_root = Path(__file__).parent.parent.parent
    host_bin = project_root / "bld" / "host" / "bin"
    env["PATH"] = f"{host_bin}:{env.get('PATH', '')}"
    return env

def target_configure(staging_dir: Path, target_dir: Path, arch="x32"):
    colors.info(f"mxmux: target_configure ({arch})")
    repo_root = Path(__file__).parent
    build_path = repo_root / f"build-{arch}"
    build_path.mkdir(parents=True, exist_ok=True)
    
    project_root = repo_root.parent.parent
    musl_cfg = project_root / "bld" / f"musl_{arch}.cfg"

    cmd = [
        "cmake",
        "-G", "Ninja",
        "-S", ".", 
        "-B", f"build-{arch}",
        "-DCMAKE_INSTALL_PREFIX=/usr",
        "-DCMAKE_BUILD_TYPE=Release",
        
        # Cross-compilation settings using our clang/musl config
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
        f"-DCMAKE_C_FLAGS={os.environ.get('CFLAGS', '')}",
        f"-DCMAKE_CXX_FLAGS={os.environ.get('CXXFLAGS', '')}",
    ]

    subprocess.run(cmd, cwd=repo_root, env=get_env(), check=True)

def target_build(staging_dir: Path, target_dir: Path, arch="x32"):
    colors.info(f"mxmux: target_build ({arch})")
    repo_root = Path(__file__).parent
    build_path = repo_root / f"build-{arch}"
    
    make_jobs = multiprocessing.cpu_count()
    subprocess.run(["ninja", f"-j{make_jobs}"], cwd=build_path, env=get_env(), check=True)

def target_install(staging_dir: Path, target_dir: Path, arch="x32"):
    colors.info(f"mxmux: target_install ({arch})")
    repo_root = Path(__file__).parent
    build_path = repo_root / f"build-{arch}"
    
    # 1. Install to staging (useful if other projects need it)
    subprocess.run(["env", f"DESTDIR={staging_dir}", "ninja", "install"], cwd=build_path, env=get_env(), check=True)
    
    # 2. Install to target (for the final OS image)
    subprocess.run(["env", f"DESTDIR={target_dir}", "ninja", "install"], cwd=build_path, env=get_env(), check=True)
