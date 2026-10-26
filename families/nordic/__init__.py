# Copyright 2026-present Seeed Studio
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Nordic nRF chip family (nRF52 Arduino BSPs + nRF54 Zephyr/NCS)."""

from os.path import join

from platform_cfg import nrf_cfg
from ..base import ChipFamily


class NordicFamily(ChipFamily):
    family_ids = ("nrf",)
    owned_packages = ("tool-nrfjprog", "tool-jlink")

    # The board-level builder skeleton is the official platform-nordicnrf52
    # builder/main.py (env assembly, target_firm matrix, upload dispatch),
    # loaded through the upstream-platform machinery in ChipFamily. Our
    # deltas live in families/nordic/build.py (wrapper) and upload.py.
    upstream_platform = "nordicnrf52"
    upstream_version = (
        "https://github.com/platformio/platform-nordicnrf52.git"
        "#2df21c1424059ad549d13a30d79870103b4dbb68"  # v11.0.0
    )

    def configure_default_packages(self, platform, variables, targets):
        nrf_cfg.configure_nrf_default_packages(platform, variables, targets)
        # Every nrf build (arduino, zephyr, bare) runs the upstream
        # builder skeleton; make sure the locked checkout is present.
        self._ensure_upstream_platform(platform)

    def add_default_debug_tools(self, platform, board):
        return nrf_cfg._add_nrf_default_debug_tools(platform, board)

    def configure_debug_session(self, platform, debug_config):
        nrf_cfg.configure_nrf_debug_session(debug_config)

    def build_script(self, platform, board):
        # host_dir(), not platform.get_dir(): resolving inside the
        # wrapped window would point at the upstream package.
        return join(self.host_dir(), "families", "nordic", "build.py")

    def framework_script(self, platform, framework, board):
        # mbed-BSP boards never reach the dispatcher hook path:
        # configure_default_packages repoints frameworks["arduino"]
        # ["script"] at the family-owned arduino-core-mbed.py (an older
        # upstream fork with two local fixes; upstreaming deferred).
        # Adafruit-BSP boards load the upstream adafruit.py plus the
        # hooks below; nRF54 boards are zephyr-only, no build.bsp.name.
        if framework != "arduino":
            return None
        if board.get("build.bsp.name", "") != "adafruit":
            return None
        return join(self._upstream_platform_dir(),
                    "builder", "frameworks", "arduino", "adafruit.py")

    def get_build_hooks(self, framework):
        if framework != "arduino":
            return []
        from families.nordic import arduino_hooks
        return [arduino_hooks.bsp_libraries_on_libpath,
                arduino_hooks.keep_local_wrap_set]

    def zephyr_pioplatform(self):
        return "nordicnrf52"
