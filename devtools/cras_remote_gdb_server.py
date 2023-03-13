#!/usr/bin/python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
from pathlib import Path
import sys
import time

from chromite.lib import cros_build_lib
from chromite.lib import remote_access


class GDBServer:
    def __init__(self, ssh_ip, ssh_port, gdb_port):
        self.ssh_ip = ssh_ip
        self.ssh_port = ssh_port
        self.gdb_port = gdb_port
        self.ssh_settings = remote_access.CompileSSHConnectSettings(
            **self.GetExtraSshConfig(),
        )

    def GetExtraSshConfig(self):
        return {
            "CheckHostIP": "no",
            "BatchMode": "yes",
            "LogLevel": "QUIET",
            f"LocalForward {str(self.gdb_port)}": f"localhost:{str(self.gdb_port)}",
        }

    def RunRemoteGDBServer(self):
        """Handle remote gdb server."""
        with remote_access.ChromiumOSDeviceHandler(
            self.ssh_ip,
            port=self.ssh_port,
            connect_settings=self.ssh_settings,
        ) as device:
            gdbserver_cmds = [
                "gdbserver",
                "--attach",
                f":{self.gdb_port}",
                "$(ps -C cras -o pid=)",
            ]
            # gdbserver clean up command
            CleanupCmd = ["kill", "$(ps -C gdbserver -o pid=)"]

            device.RegisterCleanupCmd(CleanupCmd)
            print("clean up gdbserver")
            device.run(CleanupCmd, check=False)
            print("start runing gdbserver on dut cmd: ", gdbserver_cmds)
            device.run(gdbserver_cmds)


def main():
    parser = argparse.ArgumentParser(
        prog='remote GDB server',
        description='This tool will automatically open remote gdb for dut and attach CRAS\n',
    )
    parser.add_argument("--remote", default=None, required=True, help="ip of target machine")
    parser.add_argument("--port", default=1234, required=False, help="GDB server port")
    options = parser.parse_args()
    ssh_ip, _, ssh_port = options.remote.partition(":")
    if not ssh_port:
        ssh_port = 22
    gdb_port = int(options.port)
    ssh_port = int(ssh_port)
    server = GDBServer(ssh_ip, ssh_port, gdb_port)
    server.RunRemoteGDBServer()


if __name__ == "__main__":
    main()
