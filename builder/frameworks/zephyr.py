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

# Determine which Zephyr framework package to use.
# Default: framework-zephyr (stable, already contains nrf54lm20b + Axon NPU DTS).
# Optional: framework-zephyr-ncs330 (NCS 3.3.0 with Edge AI SDK), enabled by
# the project via board_build.zephyr_ncs330 = true on NPU / 20B boards.
# Must mirror the logic in platform_cfg/nrf_cfg.py::configure_nrf_default_packages.
fw_pkg_name = "framework-zephyr"
if board_name and ("nrf54lm20b" in board_name or "-npu" in board_name):
    try:
        use_ncs330 = str(env.GetProjectOption("board_build.zephyr_ncs330", "false"))
        if use_ncs330.lower() in ("true", "yes", "1"):
            fw_pkg_name = "framework-zephyr-ncs330"
    except Exception:
        pass

framework_dir = env.PioPlatform().get_package_dir(fw_pkg_name)
if not os.path.isdir(framework_dir):
    print("Warning: '%s' package not installed; falling back to framework-zephyr"
          % fw_pkg_name)
    fw_pkg_name = "framework-zephyr"
    framework_dir = env.PioPlatform().get_package_dir(fw_pkg_name)

print("Using Zephyr framework package: %s" % fw_pkg_name)

# Clone hal_nordic package from west.yaml if not present
platform_dir = env.PioPlatform().get_dir()
west_yml_path = join(framework_dir, "west.yml")
hal_nordic_dir = join(framework_dir, "_pio", "modules", "hal", "nordic")

# Copy custom board definitions into Zephyr framework boards directory
# so that Zephyr CMake can discover them during build configuration.
# We copy (not symlink) because Zephyr's CMake board discovery doesn't
# reliably follow symlink chains.  The copy is refreshed on every build
# so that edits to the platform's board files take effect immediately.
platform_boards_dir = join(platform_dir, "zephyr", "boards", "arm")
framework_boards_dir = join(framework_dir, "boards", "arm")

if os.path.isdir(platform_boards_dir):
    import shutil
    os.makedirs(framework_boards_dir, exist_ok=True)
    for board_name_dir in os.listdir(platform_boards_dir):
        src = join(platform_boards_dir, board_name_dir)
        dst = join(framework_boards_dir, board_name_dir)
        if not os.path.isdir(src):
            continue
        # Remove stale copy (broken symlink or outdated directory) then re-copy
        if os.path.islink(dst) or os.path.exists(dst):
            try:
                if os.path.islink(dst):
                    os.remove(dst)
                else:
                    shutil.rmtree(dst)
            except Exception:
                pass
        try:
            shutil.copytree(src, dst, symlinks=False)
        except Exception as e:
            print("Warning: failed to copy board %s: %s" % (board_name_dir, e))

import re
import time


def _inject_edge_ai_module(framework_dir, board_name_str, env_obj):
    """Inject sdk-edge-ai into the framework's west.yml for NPU boards.

    This allows platformio-build.py to discover the module via west.yml
    and pass it as a ZEPHYR_MODULE to CMake.  The injection is idempotent.
    Only injected when the project requests it via board_build.edge_ai = true,
    because the sdk-edge-ai module requires NCS 3.3.0 headers for full
    compilation.
    """
    if not board_name_str or "-npu" not in board_name_str:
        return

    # Only inject when the project explicitly requests Edge AI SDK
    try:
        use_edge_ai = str(env_obj.GetProjectOption("board_build.edge_ai", "false"))
        if use_edge_ai.lower() not in ("true", "yes", "1"):
            return
    except Exception:
        return

    west_yml = join(framework_dir, "west.yml")
    if not os.path.isfile(west_yml):
        return

    with open(west_yml, "r", encoding="utf-8") as f:
        west_data = yaml.safe_load(f)

    manifest = west_data.get("manifest", {})
    projects = manifest.get("projects", [])

    # Check if sdk-edge-ai is already present
    for proj in projects:
        if proj.get("name") == "sdk-edge-ai":
            return

    # Add sdk-edge-ai project entry
    edge_ai_entry = {
        "name": "sdk-edge-ai",
        "url": "https://github.com/nrfconnect/sdk-edge-ai",
        "revision": "main",
        "path": "modules/sdk-edge-ai",
    }
    projects.append(edge_ai_entry)
    manifest["projects"] = projects
    west_data["manifest"] = manifest

    with open(west_yml, "w", encoding="utf-8") as f:
        yaml.dump(west_data, f, default_flow_style=False, allow_unicode=True)

    print("Injected sdk-edge-ai into west.yml for Edge AI SDK support")


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
    a single clone failure."""
    west_yml = join(framework_dir, "west.yml")
    if not os.path.isfile(west_yml):
        return

    pio_dir = join(framework_dir, "_pio")

    with open(west_yml, "r", encoding="utf-8") as f:
        west_data = yaml.safe_load(f)
    manifest = west_data.get("manifest", {})
    remotes = {r["name"]: r for r in manifest.get("remotes", [])}
    default_remote = manifest.get("defaults", {}).get("remote", "")

    # Only pre-install for platforms that need hal_nordic (nordicnrf52, etc.)
    hal_platforms = {"nordicnrf52", "nordicnrf51"}
    if platform_name_hint not in hal_platforms:
        return

    print("Pre-installing Zephyr west dependencies (with retry)...")

    for proj in manifest.get("projects", []):
        name = proj.get("name", "")
        proj_path = proj.get("path", name)

        # Skip tool packages
        if proj_path.startswith("tool") or name.startswith("nrf_hw_"):
            continue

        # Allow only modules this platform needs:
        # - hal_nordic: required by all nRF boards (provides SoC DTSI files)
        # - sdk-edge-ai: Edge AI Add-on SDK (Axon NPU / Neuton), only listed
        #   in the NCS 3.3.0 package west.yml; ignored when absent
        allowed_modules = ("hal_nordic", "sdk-edge-ai")
        if name.startswith("hal_") and name != "hal_nordic":
            continue
        if name not in allowed_modules:
            continue

        dst = join(pio_dir, proj_path)
        if os.path.isdir(dst) and os.listdir(dst):
            continue
        # Remove empty placeholder so git clone can proceed
        if os.path.isdir(dst) and not os.listdir(dst):
            os.rmdir(dst)

        # Build URL
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

        revision = proj.get("revision")
        print(f"Pre-installing: {name}")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        _git_clone_with_retry(proj_url, dst, revision)

    print("Pre-install complete.")


# Inject sdk-edge-ai module for NPU boards so that both the preinstall
# step and platformio-build.py can discover it via west.yml.
_inject_edge_ai_module(framework_dir, board_name, env)

# Pre-install west dependencies with retry before platformio-build.py runs
# This ensures they exist when install-deps.py checks, avoiding its
# destructive clean_up() on any single failure.
_preinstall_west_deps(framework_dir, env.subst("$PIOPLATFORM"))

SConscript(
    join(framework_dir, "scripts", "platformio", "platformio-build.py"), exports="env")
    
if board_name and "nrf" in board_name:
    env.Replace(
        PIOPLATFORM=platform_name
    )