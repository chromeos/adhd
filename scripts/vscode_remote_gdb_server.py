#!/usr/bin/python
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import argparse
import json
import os
from pathlib import Path

from chromite.lib import toolchain
from chromite.lib import remote_access


class VscodeConfig:
    def __init__(self, board, gdb_ip, gdb_port):
        self.board = board
        self.sysroot = "/build/" + board
        self.gdb_ip = gdb_ip
        self.gdb_port = gdb_port
        self.dir = Path(__file__).resolve().parent.parent / '.vscode'
        if not Path.is_dir(self.dir):
            os.mkdir(self.dir)

    def CreateLaunchJson(self):
        """
        return launch.json json object for cras
        """
        launch_json = {"configurations": []}
        toolchains = toolchain.GetToolchainsForBoard(self.board)
        tc = list(toolchain.FilterToolchains(toolchains, "default", True))
        cross_gdb = tc[0] + "-gdb"
        config = {
            "name": f'CRAS GDB {self.board} Setting',
            "type": "cppdbg",
            "MIMode": "gdb",
            "miDebuggerPath": cross_gdb,
            "request": "launch",
            "program": "cras",
            "miDebuggerServerAddress": f'{self.gdb_ip}:{self.gdb_port}',
            "cwd": f'{self.sysroot}/usr/bin/',
            "externalConsole": True,
            "setupCommands": [
                {
                    "text": "set pagination off",
                    "description": "Disable pagination so gdb can attach without interruptions",
                    "ignoreFailures": False,
                },
                {
                    "text": f'set sysroot {self.sysroot}',
                    "description": "Set sysroot so all of the shared libs are findable",
                    "ignoreFailures": False,
                },
                {
                    "text": f'set solib-absolute-prefix {self.sysroot}',
                    "description": "Set debug file directory to get the symbols for the shared libs",
                    "ignoreFailures": False,
                },
                {
                    "text": f'set solib-search-path {self.sysroot}',
                    "description": "Shared object search paths",
                    "ignoreFailures": False,
                },
                {
                    "text": f'set debug-file-directory {self.sysroot}',
                    "description": "Debug file search paths",
                    "ignoreFailures": False,
                },
                {
                    "text": "set directories /mnt/host/source/src/third_party/adhd/cras/src/",
                    "description": "Source code search paths",
                    "ignoreFailures": False,
                },
            ],
        }
        launch_json["configurations"].append(config)
        return json.dumps(launch_json, indent=2), Path.joinpath(self.dir, Path("launch.json"))

    def CreateSettingsJson(self):
        setting_json = {"C_Cpp.intelliSenseEngine": "Disabled"}
        return json.dumps(setting_json, indent=2), Path.joinpath(self.dir, Path("settings.json"))

    def WriteConfigJson(self):
        setting_json, setting_json_path = self.CreateSettingsJson()
        launch_json, launch_json_path = self.CreateLaunchJson()
        setting_json_path.write_text(setting_json)
        launch_json_path.write_text(launch_json)


class GdbServer:
    _GDB_DUT_PORT = "1234"

    def __init__(self, ip, ssh_port):
        self.ip = ip
        self.gdb_ip = "127.0.0.1"
        self.ssh_port = ssh_port
        self.ssh_settings = remote_access.CompileSSHConnectSettings(
            **self.GetExtraSshConfig(),
        )

    def GetExtraSshConfig(self):
        return {
            "CheckHostIP": "no",
            "BatchMode": "yes",
            "LogLevel": "QUIET",
            f"LocalForward {self._GDB_DUT_PORT}": f"localhost:{self._GDB_DUT_PORT}",
        }

    def GetGDBServerIPandPort(self):
        return self.gdb_ip, self._GDB_DUT_PORT

    def RunRemoteGDBServer(self):
        """Handle remote gdb server."""

        with remote_access.ChromiumOSDeviceHandler(
            self.ip,
            port=self.ssh_port,
            connect_settings=self.ssh_settings,
        ) as device:
            device.RegisterCleanupCmd(["kill", "$(ps -C gdbserver -o pid=)"])
            gdbserver_cmds = [
                "gdbserver",
                "--attach",
                f":{self._GDB_DUT_PORT}",
                "$(ps -C cras -o pid=)",
            ]
            print("start runing gdbserver on dut cmd: ", gdbserver_cmds)
            device.run(gdbserver_cmds)


def main():
    parser = argparse.ArgumentParser(
        prog='vscode remote GDB server',
        description='This tool will automatically generate config file for vscode and open remote gdb for dut and attach CRAS\n note: .vscode under adhd dir will be replaced.',
    )
    parser.add_argument("--board", default=None, required=True, help="board to debug for")
    parser.add_argument("--remote", default=None, required=True, help="ip of target machine")
    options = parser.parse_args()
    ip, _, port = options.remote.partition(":")
    if not port:
        port = "22"
    server = GdbServer(ip, port)
    gdb_ip, gdb_port = server.GetGDBServerIPandPort()
    vscode_config = VscodeConfig(options.board, gdb_ip, gdb_port)
    vscode_config.WriteConfigJson()
    while True:
        server.RunRemoteGDBServer()


if __name__ == "__main__":
    main()
