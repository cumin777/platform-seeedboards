import sys
IS_WINDOWS = sys.platform.startswith("win")


def configure_stm32_default_packages(self, variables, targets):
    upload_protocol = ""
    board = variables.get("board")
    frameworks = variables.get("pioframework", [])

    if board:
        upload_protocol = variables.get(
            "upload_protocol",
            self.board_config(board).get("upload.protocol", ""))

        self.packages["toolchain-gccarmnoneeabi"]["optional"] = False

        if "zephyr" in frameworks:
            for p in self.packages:
                if p in ("tool-cmake", "tool-dtc", "tool-ninja"):
                    self.packages[p]["optional"] = False
            self.packages["toolchain-gccarmnoneeabi"]["version"] = "~1.80201.0"
            if not IS_WINDOWS:
                self.packages["tool-gperf"]["optional"] = False

    # configure J-LINK tool (keep for potential future use)
    jlink_conds = [
        "jlink" in variables.get(option, "")
        for option in ("upload_protocol", "debug_tool")
    ]
    if board:
        board_config = self.board_config(board)
        jlink_conds.extend([
            "jlink" in board_config.get(key, "")
            for key in ("debug.default_tools", "upload.protocol")
        ])
    jlink_pkgname = "tool-jlink"
    if not any(jlink_conds) and jlink_pkgname in self.packages:
        del self.packages[jlink_pkgname]


def _add_stm32_default_debug_tools(self, board):
    debug = board.manifest.get("debug", {})
    upload_protocols = board.manifest.get("upload", {}).get(
        "protocols", [])
    if "tools" not in debug:
        debug["tools"] = {}

    # Only support stlink for STM32
    for link in ("stlink",):
        if link not in upload_protocols or link in debug['tools']:
            continue

        openocd_target = debug.get("openocd_target")
        server_args = [
            "-s", "$PACKAGE_DIR/openocd/scripts",
            "-f", "interface/%s.cfg" % link
        ]
        if openocd_target:
            if openocd_target.startswith("$") or "/" in openocd_target or "\\" in openocd_target:
                server_args.extend(["-f", openocd_target])
            else:
                server_args.extend(["-f", "target/%s" % openocd_target])

        server_args.extend([
            "-c",
            "transport select hla_swd; set WORKAREASIZE 0x4000"
        ])

        debug["tools"][link] = {
            "server": {
                "package": "tool-openocd",
                "executable": "bin/openocd",
                "arguments": server_args
            }
        }

        debug["tools"][link]["onboard"] = link in debug.get("onboard_tools", [])
        debug["tools"][link]["default"] = link in debug.get("default_tools", [])

    board.manifest["debug"] = debug
    return board


def configure_stm32_debug_session(self, debug_config):
    if debug_config.speed:
        server_executable = (debug_config.server or {}).get("executable", "").lower()
        if "openocd" in server_executable:
            debug_config.server["arguments"].extend(
                ["-c", "adapter speed %s" % debug_config.speed]
            )
