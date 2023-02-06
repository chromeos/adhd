#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Strict linter for device specific UCM files
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import difflib
import enum
import logging
import os
import pathlib
from typing import List, Optional


@dataclasses.dataclass
class Diagnostic:
    path: str
    line: int
    message: str


class DiagnosticConsumer:
    def add(self, path: str, line: int, message: str):
        pass


class LoggingDiagnostics(DiagnosticConsumer):
    def add(self, path: str, line: int, message: str):
        if line:
            logging.warning(f'Line {line}: {message}')
        else:
            logging.warning(message)


class GerritComment(DiagnosticConsumer):
    def __init__(self) -> None:
        self.diagnostics = []

    def add(self, path: str, line: int, message: str):
        self.diagnostics.append(Diagnostic(path, line, message))

    def as_json_compatible(self):
        return [
            {
                'unresolved': True,
                **dataclasses.asdict(diag),
            }
            for diag in self.diagnostics
        ]


@dataclasses.dataclass
class MultiDiag(DiagnosticConsumer):
    consumers: List[DiagnosticConsumer]

    def add(self, path: str, line: int, message: str):
        for consumer in self.consumers:
            consumer.add(path, line, message)


gerrit_json_diags = GerritComment()
diags = MultiDiag([LoggingDiagnostics(), gerrit_json_diags])


class k(str, enum.Enum):
    UCM_CONFIG = 'ucm-config'
    AUDIO = 'audio'
    FILES = 'files'
    CHROMEOS_BSP_ = 'chromeos-bsp-'
    CHROMEOS_BASE = 'chromeos-base'
    OVERLAY_ = 'overlay-'


def path_check_exact(p: pathlib.Path, name: str):
    if p.name != name:
        diags.add(str(p), 0, f'`{p}` should be named as `{name}`')


def path_check_prefix(p: pathlib.Path, name: str):
    if not p.name.startswith(name):
        diags.add(str(p), 0, f'`{p}` should have prefix `{name}`')
        return
    return p.name[len(name) :]


def lint_card_conf(path: pathlib.Path, relpath: str, project_name: str):
    actual_content = path.read_text(encoding='ascii')
    expected_content = f'''\
Comment "{project_name.capitalize()} internal card"

SectionUseCase."HiFi" {{
\tFile "HiFi.conf"
\tComment "Default"
}}
'''
    if actual_content != expected_content:
        diff = '\n'.join(difflib.ndiff(actual_content.splitlines(), expected_content.splitlines()))
        diags.add(relpath, 0, f'Expected {relpath} content:\n```{diff}\n```')


class TreeNode:
    """Node of the parse tree of an UCM config"""


@dataclasses.dataclass
class HiFiConf(TreeNode):
    """The root node of the parse tree of a UCM config"""

    verb: Section
    devices: list[Section]
    path: str


@dataclasses.dataclass
class Section(TreeNode):
    """A SectionVerb or SectionDevice"""

    name: str
    value: Block
    enable_sequence: Block
    disable_sequence: Block
    path: str
    lineinfo: tuple[int, str]

    def warning(self, msg):
        diags.add(self.path, self.lineinfo[0], msg)


@dataclasses.dataclass
class Block(TreeNode):
    """A Value, EnableSequence or DisableSequence block"""

    items: list[Item]
    path: str
    lineinfo: tuple[int, str]

    def warning(self, msg):
        diags.add(self.path, self.lineinfo[0], f'{msg}')

    def get(self, key) -> Optional[Item]:
        for item in self.items:
            if item.lhs == key:
                return item


@dataclasses.dataclass
class Item(TreeNode):
    lhs: str
    raw_rhs: str
    rhs: str
    path: str
    lineinfo: tuple[int, str]

    def __init__(self, lhs, raw_rhs, path, lineinfo):
        self.lhs = lhs
        self.raw_rhs = raw_rhs.strip()
        self.path = path
        self.lineinfo = lineinfo

        if self.raw_rhs != raw_rhs:
            self.warning('redundant whitespace around value')
        if not self.raw_rhs.startswith('"') or not self.raw_rhs.endswith('"'):
            self.warning('improperly quoted value')
            self.rhs = '<can not parse>'
        self.rhs = self.raw_rhs[1:-1]

    def warning(self, msg):
        diags.add(self.path, self.lineinfo[0], f'{msg}')


class HiFiSyntaxError(Exception):
    pass


class HiFiParser:
    def __init__(self, lines, path):
        self.path = path
        self.lines = collections.deque(
            (i, l)
            for (i, l) in enumerate(lines, 1)
            if l.strip()
            # drop comments
            if not l.lstrip().startswith('#')
        )

    @property
    def line(self):
        return self.lines[0][1]

    @property
    def lineno(self):
        return self.lines[0][0]

    def getline(self, indent: int):
        if not self.line.startswith('\t' * indent):
            self.warning(f'Want {indent} tabs')
        return self.line.strip()

    def warning(self, msg):
        diags.add(self.path, self.lineno, msg)

    def error(self, msg):
        raise HiFiSyntaxError(f'Line {self.lineno}: {msg}\n{self.line}')

    def expect(self, indent: int, exact: str):
        if self.line == exact:
            return self.lines.popleft()
        self.error(f'expecting {exact!r}, got {self.line!r}\n> {self.line}')

    def parse(self) -> Optional[HiFiConf]:
        try:
            return self.parse_hifi()
        except HiFiSyntaxError as e:
            logging.error(str(e))

    def parse_hifi(self) -> HiFiConf:
        lineinfo = self.expect(0, 'SectionVerb {')
        verb = Section('SectionVerb', *self.parse_blocks(), self.path, lineinfo)
        self.expect(0, '}')

        devices = []
        while self.lines:
            start = 'SectionDevice."'
            end = '".0 {'
            if not self.line.startswith(start):
                self.error(f'expecting starting with `{start}`, got `{self.line}`')
            if not self.line.endswith(end):
                self.error(f'expecting ending with `{end}`, got `{self.line}`')
            lineinfo = self.lines.popleft()

            devices.append(
                Section(
                    lineinfo[1][len(start) : -len(end)],
                    *self.parse_blocks(),
                    self.path,
                    lineinfo,
                )
            )

            self.expect(0, '}')

        return HiFiConf(verb, devices, self.path)

    def parse_blocks(self) -> tuple[Block, Block, Block]:
        return (
            self.parse_block('Value', '{', '}'),
            self.parse_block('EnableSequence', '[', ']'),
            self.parse_block('DisableSequence', '[', ']'),
        )

    def parse_block(self, block_name, lp, rp) -> Block:
        want = block_name + ' ' + lp
        dedented = self.getline(1)
        if dedented != want:
            self.error(f'expecting `{want}` but got `{dedented}`')
        lineinfo = self.lines.popleft()  # consume lp

        kvs = []
        while self.line.strip() != rp:
            kvs.append(self.parse_Item())
        self.lines.popleft()  # consume rp

        return Block(kvs, self.path, lineinfo)

    def parse_Item(self) -> Item:
        line = self.getline(2)
        k, v = line.split(' ', 1)
        return Item(k, v, self.path, self.lines.popleft())


def lint_hifi_conf(path: pathlib.Path, relpath: str, card_name: str):
    hifi = HiFiParser(path.read_text(encoding='ascii').splitlines(), path=relpath).parse()
    if hifi is None:
        return

    if len(hifi.verb.value.items) != 1:
        hifi.verb.value.warning('Should have exactly one item: FullySpecifiedUCM "1"')
    kv = hifi.verb.value.items[0]
    if kv.lhs != 'FullySpecifiedUCM' or kv.rhs != '1':
        kv.warning('Should have exactly one item: FullySpecifiedUCM "1"')
    lint_sequences(card_name, hifi.verb.enable_sequence)
    lint_sequences(card_name, hifi.verb.disable_sequence)

    for device in hifi.devices:
        lint_device(card_name, device)


class DeviceType(enum.Enum):
    Speaker = enum.auto()
    Headphone = enum.auto()
    InternalMic = enum.auto()
    Mic = enum.auto()
    HDMI = enum.auto()
    BluetoothPCMIn = enum.auto()
    BluetoothPCMOut = enum.auto()

    @classmethod
    def fromstring(cls, name: str) -> Optional[DeviceType]:
        try:
            return {
                'Speaker': DeviceType.Speaker,
                'Headphone': DeviceType.Headphone,
                'Internal Mic': DeviceType.InternalMic,
                'Front Mic': DeviceType.InternalMic,
                'Rear Mic': DeviceType.InternalMic,
                'Mic': DeviceType.Mic,
                'SCO Line In': DeviceType.BluetoothPCMIn,
                'SCO Line Out': DeviceType.BluetoothPCMOut,
            }[name]
        except KeyError:
            pass
        if 'HDMI' in name:
            return DeviceType.HDMI


def lint_device(card_name: str, dev: Section):
    t = DeviceType.fromstring(dev.name)
    if t is None:
        dev.warning(f'invalid device name `{dev.name}`')
        return

    # check PlaybackPCM, CapturePCM
    pcm_prefix = f'hw:{get_card_id(card_name)},'
    if t in {
        DeviceType.Headphone,
        DeviceType.Speaker,
        DeviceType.HDMI,
        DeviceType.BluetoothPCMOut,
    }:
        wantPCM = 'PlaybackPCM'
        unwantedPCM = 'CapturePCM'
    else:
        wantPCM = 'CapturePCM'
        unwantedPCM = 'PlaybackPCM'
    if pcm := dev.value.get(wantPCM):
        if not pcm.rhs.startswith(pcm_prefix):
            pcm.warning(f'`{wantPCM}` should have prefix `{pcm_prefix}`')
    else:
        dev.value.warning(f'Value `{wantPCM}` is required for `{dev.name}`')
    if pcm := dev.value.get(unwantedPCM):
        pcm.warning(f'`{unwantedPCM}` is invalid for `{dev.name}`')

    # check JackDev
    if t in {DeviceType.Headphone, DeviceType.Mic, DeviceType.HDMI}:
        if jack_dev := dev.value.get('JackDev'):
            if not jack_dev.rhs.startswith(card_name + ' '):
                jack_dev.warning(f'`JackDev` should start with `{card_name}`. Check with `evtest`.')
        else:
            dev.warning(
                f'Value `JackDev` is required for `{dev.name}`. Run `evtest` to check the name.'
            )

    # check JackSwitch
    # include/uapi/linux/input-event-codes.h
    want_jack_switch = None
    if t == DeviceType.Headphone:
        want_jack_switch = 2
    elif t == DeviceType.Mic:
        want_jack_switch = 4
    if want_jack_switch is not None:
        if jack_switch := dev.value.get('JackSwitch'):
            if jack_switch.rhs != str(want_jack_switch):
                jack_switch.warning(
                    f'JackSwitch `{want_jack_switch}` required for `{dev.name}`. '
                    'See "Switch events" in include/uapi/linux/input-event-codes.h'
                )
        else:
            dev.value.warning(
                f'Value JackSwitch `{want_jack_switch}` required for `{dev.name}`. '
                'See "Switch events" in include/uapi/linux/input-event-codes.h'
            )

    # check EDIDFile
    if t == DeviceType.HDMI:
        if dev.value.get('EDIDFile') is None:
            dev.value.warning(f'Value EDIDFile is required for `{dev.name}`.')

    # check PlaybackChannels
    if ch := dev.value.get('PlaybackChannels'):
        ch.warning('PlaybackChannels is only used for workarounds')

    lint_sequences(card_name, dev.enable_sequence)
    lint_sequences(card_name, dev.disable_sequence)


def get_card_id(card_name: str):
    return card_name.replace("-", "").replace("_", "")[:15]


def lint_sequences(card_name: str, seq: Block):
    hwarg = f'hw:{get_card_id(card_name)}'
    expected_cdev = f'cdev "{hwarg}"'

    if not seq.items:
        # Allow empty EnableSequence / DisableSequence
        return

    kv = seq.items[0]
    if kv.lhs != 'cdev' or kv.rhs != hwarg:
        diags.add(seq.path, seq.lineinfo[0] + 1, f'first item should be `{expected_cdev}`')


def ucmlint(directory_str):
    d = pathlib.Path(directory_str).absolute()
    assert d.is_dir()

    ucm_name = d.name
    card_name, dot, ucm_suffix = ucm_name.partition('.')
    if not dot:
        ucm_suffix = None
    logging.info(f'Assuming card name: {card_name}')
    logging.info(f'Assuming UCM suffix: {ucm_suffix}')

    # check directory names
    path_check_exact(d.parent, k.UCM_CONFIG)
    path_check_exact(d.parent.parent, k.AUDIO)
    proj_dir = d.parent.parent.parent
    logging.info(f'Assuming project: {proj_dir.name}')
    path_check_exact(proj_dir.parent, k.FILES)
    bsp_dir = proj_dir.parent.parent
    board0 = path_check_prefix(bsp_dir, k.CHROMEOS_BSP_)
    path_check_exact(bsp_dir.parent, k.CHROMEOS_BASE)
    board1 = path_check_prefix(bsp_dir.parent.parent, k.OVERLAY_)
    if board0 is None and board1 is None:
        logging.warning(f'Cannot infer board name')
        board = None
    else:
        if board0 != board1:
            logging.warning(f'Disagreeing board names: `{board0}`, `{board1}`')
        board = board0 or board1
        logging.info(f'Assuming board: {board}')

    # check ${ucm_name}.conf
    ucm_conf = d / (ucm_name + '.conf')
    if not ucm_conf.exists():
        logging.error(f'Missing {ucm_conf}')
    else:
        lint_card_conf(
            ucm_conf,
            relpath=os.path.join(directory_str, ucm_conf.name),
            project_name=proj_dir.name,
        )

    hifi_conf = d / 'HiFi.conf'
    if not hifi_conf.exists():
        logging.error(f'Missing {hifi_conf}')
    else:
        lint_hifi_conf(hifi_conf, os.path.join(directory_str, 'HiFi.conf'), card_name)


def main():
    logging.basicConfig(level=logging.INFO, format='[%(levelname)8s] %(message)s')

    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('directory')
    args = parser.parse_args()
    ucmlint(args.directory)


if __name__ == '__main__':
    main()
