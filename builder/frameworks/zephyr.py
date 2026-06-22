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


def _apply_framework_patches(framework_dir):
    """Apply surgical, idempotent patches to the Zephyr framework package modules.

    The published framework-zephyr package ships upstream module files that need
    small fixes for this platform. Patching here (in the repo, distributed to all
    users) — rather than editing the installed package in place — means every
    install gets the fixes automatically and they survive package reinstalls.

    All patches are idempotent: guarded on a patched marker, so they run once.

    Patches:
      1. modules/cmsis-nn/CMakeLists.txt — LSTM glob *_s16.c -> *.c and add a
         FullyConnected *_s8_s64.c glob. Without these the CMSIS-NN s8 LSTM
         kernel (arm_lstm_unidirectional_s8) and arm_vector_sum_s8_s64 are never
         compiled, breaking the link of any CMSIS-NN + LSTM sample.
      2. modules/tflite-micro/CMakeLists.txt — add the Signal library spectral
         frontend kernels (signal/micro/kernels) + their signal/src DSP impls.
         The upstream module only compiles rfft/window/kissfft; micro_speech
         needs the full frontend (energy/filter_bank/fft_auto_scale/pcan/...).
      3. _pio/modules/sdk-edge-ai/cmake/version.cmake — replace the static
         "unknown" commit stub with a dynamic git short-hash so the runtime
         banner reads e.g. v2.1.0-<commit> instead of v2.1.0-unknown.
    """
    P = os.path.join

    def _read(path):
        with open(path, "r", encoding="utf-8") as f:
            return f.read()

    def _write(path, content):
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

    # ---- 1. cmsis-nn LSTM / FullyConnected glob fix ----
    cmsis_cmake = P(framework_dir, "modules", "cmsis-nn", "CMakeLists.txt")
    if os.path.isfile(cmsis_cmake):
        c = _read(cmsis_cmake)
        changed = False
        if "Source/LSTMFunctions/*.c\"" not in c and \
                "Source/LSTMFunctions/*_s16.c\"" in c:
            c = c.replace("Source/LSTMFunctions/*_s16.c\"",
                          "Source/LSTMFunctions/*.c\"", 1)
            changed = True
            print("  framework patch [cmsis-nn/LSTM glob]: applied")
        if "FullyConnectedFunctions/*_s8_s64.c" not in c:
            fc_old = ('    file(GLOB SRC_S16 "${CMSIS_NN_DIR}/Source/'
                      'FullyConnectedFunctions/*_s16*.c")\n'
                      '    zephyr_library_sources(${SRC_S4} ${SRC_S8} '
                      '${SRC_S16})')
            fc_new = ('    file(GLOB SRC_S16 "${CMSIS_NN_DIR}/Source/'
                      'FullyConnectedFunctions/*_s16*.c")\n'
                      '    file(GLOB SRC_S64 "${CMSIS_NN_DIR}/Source/'
                      'FullyConnectedFunctions/*_s8_s64.c")\n'
                      '    zephyr_library_sources(${SRC_S4} ${SRC_S8} '
                      '${SRC_S16} ${SRC_S64})')
            if fc_old in c:
                c = c.replace(fc_old, fc_new, 1)
                changed = True
                print("  framework patch [cmsis-nn/FC s8_s64 glob]: applied")
        if changed:
            _write(cmsis_cmake, c)

    # ---- 2. tflite-micro Signal frontend kernels + DSP impls ----
    # Zephyr builds the CLONED module's zephyr/CMakeLists.txt (the framework
    # modules/tflite-micro/CMakeLists.txt is only a static reference sourced
    # under `if 0`). Apply the Signal-kernel patch to BOTH so it takes effect
    # whichever is used. Idempotent (guarded).
    def _patch_tflm(tflm_cmake):
        if not os.path.isfile(tflm_cmake):
            return
        c = _read(tflm_cmake)
        changed = False
        T = "${TENSORFLOW_LITE_MICRO_DIR}/signal"
        mk_frontend = [
            "fft_auto_scale_kernel.cc", "fft_auto_scale_common.cc",
            "energy.cc", "energy_flexbuffers_generated_data.cc",
            "filter_bank.cc", "filter_bank_flexbuffers_generated_data.cc",
            "filter_bank_square_root.cc", "filter_bank_square_root_common.cc",
            "filter_bank_spectral_subtraction.cc",
            "filter_bank_spectral_subtraction_flexbuffers_generated_data.cc",
            "pcan.cc", "pcan_flexbuffers_generated_data.cc",
            "filter_bank_log.cc", "filter_bank_log_flexbuffers_generated_data.cc",
        ]
        src_impls = [
            "fft_auto_scale.cc", "energy.cc", "filter_bank.cc",
            "filter_bank_square_root.cc", "filter_bank_spectral_subtraction.cc",
            "filter_bank_log.cc", "pcan_argc_fixed.cc", "log.cc",
            "msb_32.cc", "msb_64.cc", "square_root_32.cc", "square_root_64.cc",
            "max_abs.cc", "circular_buffer.cc",
        ]
        # micro/kernels frontend: insert between window_flexbuffers and rfft_float
        if "%s/micro/kernels/fft_auto_scale_kernel.cc" % T not in c:
            anchor = ("    %s/micro/kernels/window_flexbuffers_generated_data.cc\n"
                      "    %s/src/rfft_float.cc" % (T, T))
            insertion = ("    %s/micro/kernels/window_flexbuffers_generated_data.cc\n"
                         "%s\n    %s/src/rfft_float.cc" % (
                             T,
                             "\n".join("    %s/micro/kernels/%s" % (T, n)
                                       for n in mk_frontend),
                             T))
            if anchor in c:
                c = c.replace(anchor, insertion, 1)
                changed = True
                print("  framework patch [tflite-micro/signal micro-kernels]: applied")
        # signal/src DSP impls: insert between kiss_fft_int32 and error_reporter
        if "%s/src/fft_auto_scale.cc" % T not in c:
            anchor = ("    %s/src/kiss_fft_wrappers/kiss_fft_int32.cc\n"
                      "    %s/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc"
                      % (T, T))
            insertion = ("    %s/src/kiss_fft_wrappers/kiss_fft_int32.cc\n"
                         "%s\n    %s/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc"
                         % (T,
                            "\n".join("    %s/src/%s" % (T, n) for n in src_impls),
                            T))
            if anchor in c:
                c = c.replace(anchor, insertion, 1)
                changed = True
                print("  framework patch [tflite-micro/signal src impls]: applied")
        if changed:
            _write(tflm_cmake, c)

    _patch_tflm(P(framework_dir, "modules", "tflite-micro", "CMakeLists.txt"))
    _patch_tflm(P(framework_dir, "_pio", "modules", "lib", "tflite-micro",
                  "zephyr", "CMakeLists.txt"))

    # ---- 3. sdk-edge-ai version.cmake commit string ----
    ea_vcmake = P(framework_dir, "_pio", "modules", "sdk-edge-ai", "cmake",
                  "version.cmake")
    if os.path.isfile(ea_vcmake):
        c = _read(ea_vcmake)
        if "rev-parse --short HEAD" not in c and \
                'EDGE_AI_COMMIT_STRING \\"unknown\\"' in c:
            stub_old = (
                '# Stub edge_ai_commit.h (real one needs sdk-nrf/gen_commit_h.cmake)\n'
                'file(WRITE ${_gen_dir}/edge_ai_commit.h\n'
                '  "#define EDGE_AI_COMMIT_STRING \\"unknown\\"\\n")')
            stub_new = (
                '# Generate edge_ai_commit.h from the module git commit (PIO\n'
                '# adaptation; upstream uses sdk-nrf/gen_commit_h.cmake which PIO\n'
                '# does not ship, so derive the short hash directly).\n'
                'find_package(Git QUIET)\n'
                'set(_edge_ai_commit "unknown")\n'
                'if(GIT_FOUND)\n'
                '  execute_process(\n'
                '    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD\n'
                '    WORKING_DIRECTORY ${_edge_ai_dir}\n'
                '    OUTPUT_VARIABLE _edge_ai_git_commit\n'
                '    OUTPUT_STRIP_TRAILING_WHITESPACE\n'
                '    ERROR_QUIET\n'
                '    RESULT_VARIABLE _commit_rc)\n'
                '  if(_commit_rc EQUAL 0 AND NOT "${_edge_ai_git_commit}" STREQUAL "")\n'
                '    set(_edge_ai_commit "${_edge_ai_git_commit}")\n'
                '  endif()\n'
                'endif()\n'
                'file(WRITE ${_gen_dir}/edge_ai_commit.h\n'
                '  "#define EDGE_AI_COMMIT_STRING \\"${_edge_ai_commit}\\"\\n")')
            if stub_old in c:
                c = c.replace(stub_old, stub_new, 1)
                _write(ea_vcmake, c)
                print("  framework patch [sdk-edge-ai/version commit]: applied")


def _inject_tflite_micro_module(framework_dir, board_name_str):
    """Inject tflite-micro into the framework west.yml for NPU boards.

    The published framework-zephyr west.yml does NOT list tflite-micro, so
    install-deps never fetches it and ZEPHYR_MODULES omits it — TFLM builds
    then fail with "tensorflow/lite/micro/micro_log.h: No such file". The
    tflite-micro Zephyr module (zephyrproject-rtos fork, zephyr-v4.1.0) is
    pinned to the commit verified with the 8 TFLM samples. Idempotent.
    """
    if not board_name_str or "-npu" not in board_name_str:
        return
    west_yml = join(framework_dir, "west.yml")
    if not os.path.isfile(west_yml):
        return
    with open(west_yml, "r", encoding="utf-8") as f:
        west_data = yaml.safe_load(f)
    manifest = west_data.get("manifest", {})
    projects = manifest.get("projects", [])
    if any(p.get("name") == "tflite-micro" for p in projects):
        return
    projects.append({
        "name": "tflite-micro",
        "url": "https://github.com/zephyrproject-rtos/tflite-micro",
        "revision": "8d404de73acf7687831e16d88e86e4f73cfddf8e",
        "path": "modules/lib/tflite-micro",
    })
    manifest["projects"] = projects
    west_data["manifest"] = manifest
    with open(west_yml, "w", encoding="utf-8") as f:
        yaml.dump(west_data, f, default_flow_style=False, allow_unicode=True)
    print("Injected tflite-micro into west.yml (zephyrproject-rtos @8d404de)")


def _cleanup_stray_modules(framework_dir, env_obj):
    """Remove stray module entries from the framework west.yml that cannot be
    satisfied on this platform, so install-deps.py does not try to clone them
    (a failed clone triggers install-deps' destructive clean_up that wipes ALL
    modules — the root cause of "TENSORFLOW_LITE_MICRO ... depends on 0").

    - ncs-compat: a local-only module (not a public git repo). It is provided
      by copying platform/zephyr/modules/ncs-compat/ on branches that support
      it; otherwise any leftover west.yml entry must be stripped, else
      install-deps fails cloning github.com/zephyrproject-rtos/ncs-compat.git
      (Repository not found).
    - sdk-edge-ai: when board_build.edge_ai is not requested, strip any leftover
      entry so install-deps does not clone the (large) SDK.
    Entries are only stripped when their on-disk module dir is absent (so a
    locally-provided module is kept and install-deps skips cloning it).
    """
    west_yml = join(framework_dir, "west.yml")
    if not os.path.isfile(west_yml):
        return
    with open(west_yml, "r", encoding="utf-8") as f:
        west_data = yaml.safe_load(f)
    manifest = west_data.get("manifest", {})
    projects = manifest.get("projects", [])

    try:
        use_edge_ai = str(env_obj.GetProjectOption(
            "board_build.edge_ai", "false")).lower() in ("true", "yes", "1")
    except Exception:
        use_edge_ai = False

    def _present(rel):
        d = join(framework_dir, "_pio", "modules", rel)
        return os.path.isdir(d) and bool(os.listdir(d))

    def _should_remove(p):
        name = p.get("name", "")
        if name == "ncs-compat" and not _present("ncs-compat"):
            return True
        if name == "sdk-edge-ai" and not use_edge_ai and not _present("sdk-edge-ai"):
            return True
        return False

    new_projects = [p for p in projects if not _should_remove(p)]
    removed = len(projects) - len(new_projects)
    if removed:
        manifest["projects"] = new_projects
        west_data["manifest"] = manifest
        with open(west_yml, "w", encoding="utf-8") as f:
            yaml.dump(west_data, f, default_flow_style=False, allow_unicode=True)
        print("Stripped %d stray module(s) from west.yml "
              "(ncs-compat/sdk-edge-ai not locally provided)" % removed)


# Inject sdk-edge-ai module for NPU boards so that both the preinstall
# step and platformio-build.py can discover it via west.yml.
_inject_edge_ai_module(framework_dir, board_name, env)

# Inject tflite-micro (TFLM source) for NPU boards — the published framework
# west.yml omits it, so it must be added here or TFLM builds can't find headers.
_inject_tflite_micro_module(framework_dir, board_name)

# Strip stray module entries (ncs-compat / sdk-edge-ai) that cannot be cloned,
# to prevent install-deps' destructive clean_up from wiping all modules.
_cleanup_stray_modules(framework_dir, env)

# Pre-install west dependencies with retry before platformio-build.py runs
# This ensures they exist when install-deps.py checks, avoiding its
# destructive clean_up() on any single failure.
_preinstall_west_deps(framework_dir, env.subst("$PIOPLATFORM"))

# Apply idempotent framework-package patches (cmsis-nn glob, tflite-micro Signal
# kernels, sdk-edge-ai version). Modules ship with the package so they are
# already present here; patching before platformio-build.py ensures CMake sees
# the fixed sources. Safe to re-run every build (guarded).
_apply_framework_patches(framework_dir)

SConscript(
    join(framework_dir, "scripts", "platformio", "platformio-build.py"), exports="env")
    
if board_name and "nrf" in board_name:
    env.Replace(
        PIOPLATFORM=platform_name
    )