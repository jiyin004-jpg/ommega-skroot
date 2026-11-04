#!/usr/bin/env python3
"""Ommega SKRoot Pro 版模块的构建脚本。

做三件事：
  1. 用 NDK 编模块壳 libommega_skroot.so（SKRoot SDK 静态链进去）；
  2. 把 A 端二进制（keymint / ommega-inject / resetprop / ommega-policy / adbrd）
     从 ommega 的发布包或构建产物里取出来，摆进 OmmegaCore/libs/arm64-v8a/；
  3. 组包成 SKRoot 认的 zip：libommega_skroot.so + webroot/ + OmmegaCore/。

A 端二进制不在本仓库里维护（它们跟 ommega 主仓库同一套），这里只负责取和摆。
用法：
  python build.py                          # 自动找最新的 A 端发布包
  python build.py --a-zip path/to/a.zip
  python build.py --a-bin-dir path/to/libs/arm64-v8a
  python build.py --skip-build             # 不重编 .so，只用上次产物
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent
MODULE_DIR = REPO / "module"
CORE = REPO / "core"
VENDOR = REPO / "vendor" / "kernel_module_kit"
DIST = REPO / "dist"
STAGE = DIST / "stage"

MODULE_SO_NAME = "libommega_skroot.so"
CORE_SUBDIR = "OmmegaCore"
ABI = "arm64-v8a"

# A 端发布包里这几个是必须有的；缺一个就说明取错包了。
REQUIRED_BINS = ("keymint", "ommega-inject", "resetprop", "ommega-policy")

# core/ 下要整份搬进 OmmegaCore/ 的东西（脚本 + 资源）。
CORE_FILES = (
    "boot.sh",
    "service.sh",
    "daemon-injector",
    "soterta.sh",
    "layout.sh",
    "sepolicy.rule",
    "sepolicy_apply.sh",
    "kmod-loader.sh",
    "adbr-daemon.sh",
    "verify.sh",
    "injector.toml",
    "keybox_tee.xml",
    "keybox_strongbox.xml",
)


def sh(cmd: list[str], **kw) -> None:
    print("$", " ".join(cmd))
    subprocess.run(cmd, check=True, **kw)


def read_version() -> str:
    return (REPO / "VERSION").read_text(encoding="utf-8").strip()


def ndk_build(ndk: str | None, skip: bool) -> Path:
    out = MODULE_DIR / "libs" / ABI / MODULE_SO_NAME
    if skip:
        if not out.exists():
            raise SystemExit(f"--skip-build 但 {out} 不存在")
        return out

    ndk = ndk or os.environ.get("ANDROID_NDK_HOME") or r"D:\android-ndk-r27d"
    ndk_path = Path(ndk)
    candidates = [ndk_path / "ndk-build.cmd", ndk_path / "ndk-build"]
    ndk_build_bin = next((p for p in candidates if p.exists()), None)
    if not ndk_build_bin:
        raise SystemExit(f"找不到 ndk-build，检查 --ndk（现在是 {ndk}）")

    # ndk-build 在 jni 目录下最省事：官方示例就是这么编的
    jni = MODULE_DIR / "jni"
    sh([str(ndk_build_bin), "NDK_PROJECT_PATH=" + str(MODULE_DIR),
        "APP_BUILD_SCRIPT=" + str(jni / "Android.mk"),
        "APP_ABI=" + ABI, "APP_PLATFORM=android-28", "-j4"], cwd=str(jni))
    if not out.exists():
        raise SystemExit(f"编译完没找到 {out}")
    return out


def _candidate_roots() -> list[Path]:
    roots: list[Path] = []
    env = os.environ.get("OMMEGA_ROOT")
    if env:
        roots.append(Path(env))
    roots += [
        REPO.parent / "ommega",
        Path.home() / "Desktop" / "ommega",
    ]
    return roots


def newest_a_zip() -> Path:
    found: list[Path] = []
    for root in _candidate_roots():
        for rel in ("a-side/source/target", "a-side/build"):
            d = root / rel
            if d.is_dir():
                found += list(d.glob("ommega-a-release-*.zip"))
    if not found:
        raise SystemExit("找不到 A 端发布包，用 --a-zip 指定，或设环境变量 OMMEGA_ROOT")
    return max(found, key=lambda p: p.stat().st_mtime)


def extract_bins_from_zip(zip_path: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            if not name.startswith("libs/" + ABI + "/"):
                continue
            base = name.rsplit("/", 1)[-1]
            if base.endswith(".sha256"):
                continue
            with z.open(name) as src, open(dest / base, "wb") as fp:
                shutil.copyfileobj(src, fp)
    print(f"取二进制: {zip_path} -> {dest}")


def copy_bins(src_dir: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for p in src_dir.iterdir():
        if p.is_file() and not p.name.endswith(".sha256"):
            shutil.copy2(p, dest / p.name)


def make_module_prop(version: str) -> str:
    # 面板要读 module.prop 拿版本号；SKRoot 自己的名片在 .so 里，这个文件只服务面板。
    return (
        "id=ommega-skroot\n"
        "name=Ommega (SKRoot)\n"
        f"version={version}\n"
        "versionCode=1\n"
        "author=jiyin004\n"
        "description=Ommega SKRoot Pro 版\n"
    )


def stage_module(so: Path, bins: Path, version: str) -> Path:
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)

    # 1) 模块壳
    shutil.copy2(so, STAGE / MODULE_SO_NAME)

    # 2) 面板
    shutil.copytree(CORE / "webroot", STAGE / "webroot")

    # 3) A 端运行时
    core_out = STAGE / CORE_SUBDIR
    core_out.mkdir()
    for name in CORE_FILES:
        src = CORE / name
        if not src.exists():
            raise SystemExit(f"core/{name} 不存在")
        shutil.copy2(src, core_out / name)

    core_libs = core_out / "libs" / ABI
    core_libs.mkdir(parents=True)
    for p in bins.iterdir():
        if p.is_file():
            shutil.copy2(p, core_libs / p.name)

    # 面板里的 RESETPROP 指向 MOD/bin/resetprop（MOD = OmmegaCore），
    # 所以 resetprop 另放一份到 bin/；属性伪装也用它。
    bin_dir = core_out / "bin"
    bin_dir.mkdir()
    shutil.copy2(core_libs / "resetprop", bin_dir / "resetprop")

    # 4) pathmask 的 .ko（SKRoot 下不保证 insmod 得动，脚本会自己记状态）
    if (CORE / "pathmask").is_dir():
        shutil.copytree(CORE / "pathmask", core_out / "pathmask")

    # 5) 面板要的 module.prop
    (core_out / "module.prop").write_text(make_module_prop(version), encoding="utf-8")

    # 6) 权限：SKRoot 解压时统一给 0777，这里再做一次是为了本地检查
    for p in core_out.rglob("*"):
        if p.is_file():
            os.chmod(p, 0o755 if (p.suffix in (".sh", "") or os.access(p, os.X_OK)) else 0o644)
    return STAGE


def pack(stage: Path, version: str) -> Path:
    DIST.mkdir(parents=True, exist_ok=True)
    out = DIST / f"ommega-skroot-{version}.zip"
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(stage.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(stage).as_posix())
    return out


def verify(so: Path, zp: Path) -> None:
    """能做的最小自检：.so 里有模块名片、zip 里有必需文件。"""
    data = so.read_bytes()
    for needle in (b"Ommega", b"ommegaskrootpro", b"OmmegaCore"):
        if needle not in data:
            raise SystemExit(f".so 里没有 {needle!r}，模块名片可能没编进去")
    with zipfile.ZipFile(zp) as z:
        names = set(z.namelist())
        need = {MODULE_SO_NAME, "webroot/index.html", "webroot/ksu-shim.js",
                f"{CORE_SUBDIR}/service.sh", f"{CORE_SUBDIR}/boot.sh",
                f"{CORE_SUBDIR}/libs/{ABI}/keymint",
                f"{CORE_SUBDIR}/libs/{ABI}/ommega-inject"}
        missing = need - names
        if missing:
            raise SystemExit("zip 里缺: " + ", ".join(sorted(missing)))
        print("zip 内容:")
        for n in sorted(names):
            info = z.getinfo(n)
            print(f"  {info.file_size:>10}  {n}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ndk", default=None)
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--a-zip", type=Path, default=None)
    ap.add_argument("--a-bin-dir", type=Path, default=None)
    ap.add_argument("--version", default=None)
    args = ap.parse_args()

    version = args.version or read_version()
    so = ndk_build(args.ndk, args.skip_build)

    bins_dir = DIST / "bins" / ABI
    if bins_dir.exists():
        shutil.rmtree(bins_dir)
    if args.a_bin_dir:
        copy_bins(args.a_bin_dir, bins_dir)
        print(f"取二进制: {args.a_bin_dir} -> {bins_dir}")
    else:
        a_zip = args.a_zip or newest_a_zip()
        extract_bins_from_zip(a_zip, bins_dir)

    have = {p.name for p in bins_dir.iterdir() if p.is_file()}
    missing = [b for b in REQUIRED_BINS if b not in have]
    if missing:
        raise SystemExit("A 端二进制缺失: " + ", ".join(missing) + f"（在 {bins_dir}）")

    stage = stage_module(so, bins_dir, version)
    zp = pack(stage, version)
    verify(so, zp)

    sha = hashlib.sha256(zp.read_bytes()).hexdigest()
    (DIST / f"{zp.name}.sha256").write_text(f"{sha}  {zp.name}\n", encoding="utf-8")
    print(f"\nOK  {zp}  ({zp.stat().st_size} bytes)\nsha256 {sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
