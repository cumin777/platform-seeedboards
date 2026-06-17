# Copyright 2019-present PlatformIO <contact@platformio.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
The Zephyr Project is a scalable real-time operating system (RTOS) supporting multiple
hardware architectures, optimized for resource constrained devices, and built with
safety and security in mind.

https://github.com/zephyrproject-rtos/zephyr
"""

from os.path import join
import subprocess
import os
from SCons.Script import Import, SConscript
try:
    import yaml
except ImportError:
    subprocess.run(["pip", "install", "pyyaml"], check=True)
    import yaml

Import("env")

platform_name = env.subst("$PIOPLATFORM")
board_name = env.get("BOARD", "")

if board_name and "nrf" in board_name:
    env.Replace(
        PIOPLATFORM="nordicnrf52"
    )

if board_name and "stm32" in board_name:
    env.Replace(
        PIOPLATFORM="ststm32"
    )

# Clone hal_nordic package from west.yaml if not present
framework_dir = env.PioPlatform().get_package_dir("framework-zephyr")
platform_dir = env.PioPlatform().get_dir()
west_yml_path = join(framework_dir, "west.yml")
hal_nordic_dir = join(framework_dir, "_pio", "modules", "hal", "nordic")

# Symlink custom board definitions into Zephyr framework boards directory
# so that Zephyr CMake can discover them during build configuration.
# Zephyr <= 4.2 uses boards/arm/<board>/, Zephyr >= 4.3 uses boards/<vendor>/<board>/
# Note: For vendor-based directories (Zephyr >= 4.3), we must copy instead of
# symlink because Zephyr's list_boards.py uses pathlib.rglob() which doesn't
# follow symlinks by default.
import shutil as _shutil

for boards_subdir in ["arm", "seeed"]:
    platform_boards_dir = join(platform_dir, "zephyr", "boards", boards_subdir)
    framework_boards_dir = join(framework_dir, "boards", boards_subdir)

    if os.path.isdir(platform_boards_dir):
        os.makedirs(framework_boards_dir, exist_ok=True)
        for board_name_dir in os.listdir(platform_boards_dir):
            src = join(platform_boards_dir, board_name_dir)
            dst = join(framework_boards_dir, board_name_dir)
            if not os.path.isdir(src):
                continue
            if boards_subdir == "arm":
                # Symlinks reflect source changes automatically; only create if missing
                if not os.path.exists(dst):
                    try:
                        os.symlink(src, dst)
                        print(f"Linked board: {board_name_dir} -> {src}")
                    except OSError:
                        _shutil.copytree(src, dst)
                        print(f"Copied board: {board_name_dir} -> {dst}")
            else:
                # Copied boards: always re-copy to pick up source changes
                if os.path.exists(dst):
                    _shutil.rmtree(dst)
                _shutil.copytree(src, dst)
                print(f"Copied board: {board_name_dir} -> {dst}")

import re
import time


def _is_commit_hash(value):
    return value and re.match(r"[0-9a-f]{7,}$", value) is not None


def _git_clone_with_retry(url, dst, revision, max_retries=3, retry_delay=5):
    """Clone a git repository with retry logic for unstable networks."""
    for attempt in range(1, max_retries + 1):
        args = ["git", "clone"]
        is_commit = _is_commit_hash(revision)
        if not is_commit and revision:
            args.extend(["--branch", revision, "--depth", "1"])
        elif not is_commit:
            args.extend(["--depth", "1"])

        try:
            print(f"  Cloning {url} (attempt {attempt}/{max_retries})")
            subprocess.run(args + [url, dst], check=True,
                           capture_output=True, text=True)
            if is_commit and revision:
                subprocess.run(
                    ["git", "-C", dst, "checkout", revision],
                    check=True, capture_output=True, text=True)
            print(f"  OK: {os.path.basename(dst)}")
            return True
        except subprocess.CalledProcessError as e:
            if os.path.isdir(dst):
                import shutil
                shutil.rmtree(dst, ignore_errors=True)
            if attempt < max_retries:
                print(f"  Failed (attempt {attempt}): {e.stderr.strip() if e.stderr else e}")
                print(f"  Retrying in {retry_delay}s...")
                time.sleep(retry_delay)
            else:
                print(f"  FAILED after {max_retries} attempts: {url}")
                return False
    return False


def _preinstall_west_deps(framework_dir, platform_name_hint):
    """Pre-install west.yml dependencies with retry so that install-deps.py
    can skip them later. This avoids the clean_up() wiping everything on
    a single clone failure.

    Strategy:
    - For each platform family, clone only the actually-needed HAL modules
      (with git clone --depth 1 + retry).
    - Create empty placeholder directories for ALL other west.yml projects so
      that install-deps.py sees them as "already installed" and skips cloning.
    - Write state.json with all revisions so future checks are instant.
    """
    west_yml = join(framework_dir, "west.yml")
    if not os.path.isfile(west_yml):
        return

    pio_dir = join(framework_dir, "_pio")

    # Determine which HAL modules are required based on platform
    hal_modules_map = {
        "nordicnrf52": ["hal_nordic"],
        "nordicnrf51": ["hal_nordic"],
        "ststm32": ["hal_stm32", "hal_st"],
    }
    needed_hals = hal_modules_map.get(platform_name_hint, [])
    if not needed_hals:
        return

    with open(west_yml, "r", encoding="utf-8") as f:
        west_data = yaml.safe_load(f)
    manifest = west_data.get("manifest", {})
    remotes = {r["name"]: r for r in manifest.get("remotes", [])}
    default_remote = manifest.get("defaults", {}).get("remote", "")
    default_remote_url = remotes.get(default_remote, {}).get("url-base", "")

    # Core modules that must have real content (not just placeholders)
    # These are needed by nearly every build
    core_modules = {"cmsis", "cmsis_6"}

    modules_to_clone = set(needed_hals) | core_modules

    print("Pre-installing Zephyr west dependencies for %s..." % platform_name_hint)

    state = {}
    for proj in manifest.get("projects", []):
        name = proj.get("name", "")
        proj_path = proj.get("path", name)
        revision = proj.get("revision", "")

        # Skip tool packages
        if proj_path.startswith("tool") or name.startswith("nrf_hw_"):
            continue

        dst = join(pio_dir, proj_path)
        state[name] = revision

        if os.path.isdir(dst) and os.listdir(dst):
            # Already has content, skip
            continue

        if name in modules_to_clone:
            # Clone with retry
            if "url" in proj:
                proj_url = proj["url"]
                if not proj_url.startswith("http"):
                    url_base = remotes.get(
                        proj.get("remote", default_remote), {}
                    ).get("url-base", "")
                    proj_url = url_base.rstrip("/") + "/" + proj_url.lstrip("/")
            else:
                url_base = remotes.get(
                    proj.get("remote", default_remote), {}
                ).get("url-base", "")
                repo_path = proj.get("repo-path", name)
                proj_url = url_base.rstrip("/") + "/" + repo_path + ".git"

            print(f"  Cloning: {name}")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            _git_clone_with_retry(proj_url, dst, revision)
        else:
            # Create empty placeholder so install-deps.py thinks it's installed
            os.makedirs(dst, exist_ok=True)

    # Also ensure _bare_module and bootloader exist (from the framework package itself)
    for extra in [join(pio_dir, "_bare_module"), join(pio_dir, "bootloader")]:
        if not os.path.isdir(extra):
            os.makedirs(extra, exist_ok=True)

    # Write state.json so install-deps.py can compare revisions efficiently
    state_file = join(pio_dir, "state.json")
    import json
    with open(state_file, "w") as f:
        json.dump(state, f, indent=2)

    print("Pre-install complete (%d modules)." % len(state))


# Pre-install west dependencies with retry before platformio-build.py runs
# This ensures they exist when install-deps.py checks, avoiding its
# destructive clean_up() on any single failure.
_preinstall_west_deps(framework_dir, env.subst("$PIOPLATFORM"))

# Inject platform-specific Zephyr modules (e.g. uf2_dfu_reset for STM32C5)
# into the ZEPHYR_EXTRA_MODULES env var so that platformio-build.py
# can append them to the CMake -DZEPHYR_MODULES list.
#
# IMPORTANT: The module must live in a path WITHOUT spaces. The platform
# directory may contain spaces (e.g. "Seeed Studio"), which breaks
# zephyr_module.py's argument parsing. We copy the module into the
# framework's _pio/modules/ directory (space-free) and use that path.
_extra_modules = []
if board_name and "stm32c5" in board_name:
    _src_mod = join(platform_dir, "zephyr", "modules", "uf2_dfu_reset")
    if os.path.isdir(_src_mod):
        _dst_mod = join(framework_dir, "_pio", "modules", "uf2_dfu_reset")
        # Always re-copy to pick up source changes during development
        if os.path.exists(_dst_mod):
            _shutil.rmtree(_dst_mod)
        _shutil.copytree(_src_mod, _dst_mod)
        _extra_modules.append(_dst_mod)
if _extra_modules:
    os.environ["ZEPHYR_EXTRA_MODULES"] = ";".join(_extra_modules)
else:
    os.environ.pop("ZEPHYR_EXTRA_MODULES", None)

# Apply platform-carried overrides onto framework-zephyr. Each file under
# zephyr/overrides/ is mirrored onto framework_dir by its relative path.
# Used to carry patches that upstream Zephyr only gained after the pinned
# framework release — e.g. the udc_stm32.c HAL2 compatibility layer for
# STM32C5 (Zephyr PR #105957, landed post-4.4.0). The patch is guarded by
# #ifdef CONFIG_STM32_HAL2 so applying it is harmless on non-C5 boards.
_overrides_root = join(platform_dir, "zephyr", "overrides")
if os.path.isdir(_overrides_root):
    for _root, _dirs, _files in os.walk(_overrides_root):
        for _f in _files:
            _src = join(_root, _f)
            _rel = os.path.relpath(_src, _overrides_root)
            _dst = join(framework_dir, _rel)
            os.makedirs(os.path.dirname(_dst), exist_ok=True)
            _shutil.copy2(_src, _dst)

SConscript(
    join(framework_dir, "scripts", "platformio", "platformio-build.py"), exports="env")
    
if board_name and "nrf" in board_name:
    env.Replace(
        PIOPLATFORM=platform_name
    )

if board_name and "stm32" in board_name:
    env.Replace(
        PIOPLATFORM=platform_name
    )