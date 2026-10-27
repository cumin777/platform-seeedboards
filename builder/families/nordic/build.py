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
# limitations under the License under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Board-level build wrapper for the Nordic family.

The skeleton (environment assembly, toolchain defaults, target_firm
matrix, upload dispatch, size/erase targets) is the upstream
platform-nordicnrf52 builder/main.py, executed via env.SConscript below
with env.PioPlatform() wrapped in an UpstreamPlatformProxy (the router
in builder/main.py mounts it around this script).

Family-specific deltas are applied around the upstream execution:

  * upload.py (this directory) pre-configures the uploader commands the
    upstream dispatch does not know (nrfutil-mcumgr / pyocd / probe-rs
    and the nRF54 RRAM OpenOCD sequences) and rewrites
    UPLOAD_PROTOCOL to the upstream "custom" passthrough branch;
  * after the upstream script returns, the MCUboot signed-image chain
    for nrfutil-mcumgr boards is grafted onto the build.

Neither the upstream file nor the framework package is modified.
"""

import sys
from os.path import join

from SCons.Script import Import

Import("env")

from families import resolve_board_family  # noqa: E402  (sys.path: host platform dir)


board = env.BoardConfig()
family = resolve_board_family(board, env.get("BOARD", ""))

# builder/ is not a package: sibling modules come into reach the same
# way builder/frameworks/zephyr.py reaches zephyr_fixes — this family's
# builder dir on sys.path (SCons deletes __file__ in SConscript frames,
# so the host path comes from family.host_dir()).
sys.path.insert(0, join(family.host_dir(), "builder", "families", "nordic"))
import upload  # noqa: E402

platform = env.PioPlatform()  # the UpstreamPlatformProxy while in here

# Our upload protocols ride the upstream "custom" passthrough branch:
# upload.py pre-configures the command, the upstream dispatch never
# inspects what it does not know, and afterwards the real artifact
# (signed image / raw binary) is grafted onto the build for the
# protocols whose flash target is not the upstream hex default.
upload_protocol = env.subst("$UPLOAD_PROTOCOL")
take_over = upload.handles(upload_protocol, board, env)

if take_over:
    upload.configure(env, platform, board, upload_protocol)
    env.Replace(UPLOAD_PROTOCOL="custom")

upstream_main = join(family._upstream_platform_dir(), "builder", "main.py")
env.SConscript(upstream_main, exports="env")

if take_over:
    upload.graft_artifacts(env, board, upload_protocol)
