#! /usr/bin/python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from http.server import BaseHTTPRequestHandler
from http.server import HTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import time

from chromite.lib import remote_access
from chromite.lib import toolchain


hostName = "localhost"
serverPort = 9090
current_folder = Path(__file__).resolve().parent
all_ucm_folder = current_folder / "for_all_boards"
device_ucm_folder = "/usr/share/alsa/ucm/"


class USBInfoParser(object):
    def __init__(self, device):
        self.device = device

    def GetPlaybackSoundCardInfo(self):
        run_result = self._getAplayStdout()
        for line in run_result.stdout.splitlines():
            if "USB Audio" in line:
                return self._parseSoundCardInfo(line)
        return None

    def GetCaptureSoundCardInfo(self):
        run_result = self._getArecordStdout()
        for line in run_result.stdout.splitlines():
            if "USB Audio" in line:
                return self._parseSoundCardInfo(line)
        return None

    def GetPlaybackMixerControls(self, card_index):
        ret = []
        for line in self._listMixers(card_index):
            name, index = self.GetMixerControlNameAndIndex(line)
            amixer_list_certain_mixer_cmd = [
                "amixer",
                f"-c{card_index}",
                "sget",
                ",".join([name, index]),
            ]
            if "pvolume" in self.device.run(amixer_list_certain_mixer_cmd).stdout:
                ret.append(line)
        return ret

    def GetCaptureMixerControls(self, card_index):
        ret = []
        for line in self._listMixers(card_index):
            name, index = self.GetMixerControlNameAndIndex(line)
            amixer_list_certain_mixer_cmd = [
                "amixer",
                f"-c{card_index}",
                "sget",
                ",".join([name, index]),
            ]
            if "cvolume" in self.device.run(amixer_list_certain_mixer_cmd).stdout:
                ret.append(line)
        return ret

    def GetMixerControlNameAndIndex(self, stdout):
        ret = {}
        mixer_name_and_index = re.match("Simple mixer control '(?P<_0>.+)',(?P<_1>.+)", stdout)
        mixer_name = mixer_name_and_index.group(1)
        mixer_index = mixer_name_and_index.group(2)
        return mixer_name, mixer_index

    def _listMixers(self, card_index):
        ret = []
        amixer_cmd = ["amixer", f"-c{card_index}", "scontrols"]
        result = self.device.run(amixer_cmd)
        return result.stdout.splitlines()

    def _parseSoundCardInfo(self, stdout):
        ret = {}
        result = re.match(
            "card (?P<_0>.+): (?P<_1>.+) \[(?P<_2>.+)\], device (?P<_3>.+): USB Audio \[USB Audio\]",
            stdout,
        )
        ret["card_index"] = result.group(1)
        ret["pcm_name"] = result.group(2)
        ret["sound_card_name"] = result.group(3)
        ret["device_index"] = result.group(4)
        return ret

    def _getAplayStdout(self):
        cmd = ["aplay", '-l']
        run_result = self.device.run(cmd)
        return run_result

    def _getArecordStdout(self):
        cmd = ["arecord", '-l']
        run_result = self.device.run(cmd)
        return run_result


class UCMConfig:
    def __init__(
        self,
        sound_card_name,
        playback_pcm,
        playback_mixer,
        capture_pcm,
        capture_mixer,
        disable_software_volume,
    ):
        self.sound_card_name = sound_card_name
        self.playback_pcm = playback_pcm
        self.playback_mixer = playback_mixer
        self.capture_pcm = capture_pcm
        self.capture_mixer = capture_mixer
        self.disable_software_volume = disable_software_volume
        self.path = all_ucm_folder / self.sound_card_name

    def GetCardNameConf(self):
        content = f"""
Comment \"{self.sound_card_name}\"

SectionUseCase.\"HiFi\" {{
        File \"HiFi.conf\"
        Comment \"Default\"
}}"""
        path = self.path / (self.sound_card_name + ".conf")
        print(path)
        print("--------------------")
        print(content)
        return content, path

    def GetHiFiConf(self):
        content = f"""
SectionVerb {{
        Value {{
                FullySpecifiedUCM \"1\""""
        if self.disable_software_volume:
            content += f"""
                DisableSoftwareVolume \"1\""""
        content += f"""
        }}

        EnableSequence [
        ]

        DisableSequence [
        ]
}}

"""
        if self.playback_pcm:
            content += f"""
SectionDevice.\"{self.sound_card_name} Output\".0 {{
        Value {{
                PlaybackPCM \"{self.playback_pcm}\"
                PlaybackMixerElem \"{self.playback_mixer}\"
        }}
}}
"""
        if self.capture_pcm:
            content += f"""
SectionDevice.\"{self.sound_card_name} Input\".0 {{
        Value {{
                CapturePCM \"{self.capture_pcm}\"
                CaptureMixerElem \"{self.capture_mixer}\"
        }}
}}
"""
        path = self.path / "HiFi.conf"
        print(path)
        print("--------------------")
        print(content)
        return content, path

    def Generate(self):
        card_name_conf, card_name_path = self.GetCardNameConf()
        hifi_conf, hifi_path = self.GetHiFiConf()
        os.makedirs(self.path, 0o755, True)
        card_name_path.write_text(card_name_conf)
        hifi_path.write_text(hifi_conf)
        return self.path


def generate_ucm(post_body_json, parser):
    playback = post_body_json["playback"]
    capture = post_body_json["capture"]
    sound_card_name = None
    playback_pcm = None
    playback_mixer = None
    capture_pcm = None
    capture_mixer = None
    disable_software_volume = False

    if playback["pcm_name"] != "":
        playback_pcm = "hw:" + playback["pcm_name"] + "," + playback["device_index"]
        mixer_name, mixer_index = parser.GetMixerControlNameAndIndex(playback["mixer_selected"])
        playback_mixer = f"{mixer_name},{mixer_index}"
        sound_card_name = playback["sound_card_name"]
    if capture["pcm_name"] != "":
        capture_pcm = "hw:" + capture["pcm_name"] + "," + capture["device_index"]
        mixer_name, mixer_index = parser.GetMixerControlNameAndIndex(capture["mixer_selected"])
        capture_mixer = f"{mixer_name},{mixer_index}"
        sound_card_name = capture["sound_card_name"]
    if post_body_json["disable_software_volume"]:
        disable_software_volume = True
    if not sound_card_name:
        raise ValueError("No sound card name")
    ucm_config = UCMConfig(
        sound_card_name,
        playback_pcm,
        playback_mixer,
        capture_pcm,
        capture_mixer,
        disable_software_volume,
    )
    return ucm_config.Generate()


class UCMDebugServerRouter(object):
    def Post(self, url, post_body_json):
        ret = {}
        if "GetUSBDeive" in url:
            ret = self._getUSBDevice(post_body_json)
        if "DeployUCM" in url:
            ret = self._deployUCM(post_body_json)
        return ret

    def _getUSBDevice(self, post_body_json):
        ret = {}
        with remote_access.ChromiumOSDeviceHandler(
            post_body_json["dut_ip"],
            port=22,
        ) as device:
            parser = USBInfoParser(device)
            playback = parser.GetPlaybackSoundCardInfo()
            if playback:
                playback["mixers"] = parser.GetPlaybackMixerControls(playback["card_index"])
            capture = parser.GetCaptureSoundCardInfo()
            if capture:
                capture["mixers"] = parser.GetCaptureMixerControls(capture["card_index"])
            ret["playback"] = playback
            ret["capture"] = capture
        return {"success": True, "data": ret}

    def _deployUCM(self, post_body_json):
        with remote_access.ChromiumOSDeviceHandler(
            post_body_json["dut_ip"],
            port=22,
        ) as device:
            parser = USBInfoParser(device)
            output_ucm_folder = generate_ucm(post_body_json, parser)
            device.CopyToDevice(output_ucm_folder, device_ucm_folder, "scp")
            device.run(["restart", "cras"])
        return {"success": True, "data": {}}


class UCMDebugServer(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        with open(current_folder / "index.html", 'r') as f:
            self.wfile.write(bytes(f.read(), "utf-8"))

    def do_POST(self):
        router = UCMDebugServerRouter()
        content_len = int(self.headers.get('Content-Length'))
        post_body_json = json.loads(self.rfile.read(content_len))
        ret = {}
        response_code = 200
        try:
            ret = router.Post(self.path, post_body_json)
        except Exception as e:
            response_code = 500
            ret = {"success": False, "data": str(e)}
            raise
        data = bytes(json.dumps(ret), "utf-8")
        self.send_response(response_code)
        self.send_header("Content-type", "application/json")
        self.send_header("Content-Length", len(data))
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    webServer = HTTPServer((hostName, serverPort), UCMDebugServer)
    print("Server started http://%s:%s" % (hostName, serverPort))

    try:
        webServer.serve_forever()
    except KeyboardInterrupt:
        pass

    webServer.server_close()
    print("Server stopped.")
