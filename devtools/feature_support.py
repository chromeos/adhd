#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""

The offline query tool of audio features support for devices.

This tool resolves queries by the core parser according to the audio config
files in ChromiumOS repo on local ends. The parser applies the common patterns
for config matching. Therefore, the queried result may be inaccurate due to
local changes in repo, or exceptional cases of config pattern by particular
devices.
"""

from __future__ import annotations

import argparse
import collections
import csv
import dataclasses
import inspect
import logging
import pathlib
from typing import Optional

from ucmlint import ucmlint
from ucmlint.ucmlint import k


PROG_DESCRIPTION = """
The offline query tool of audio features support for devices.

notes:
  * Before use, sync the local-end Chromiumos repo to the latest.
  * Results may have the chance to be misjudged by unforeseen cases for the core
    parser.
"""

PROG_EPILOG = """
usage examples:
  feature_support.py -d redrix

    * Query by the specified device 'redrix' for the list of supported features.

  feature_support.py -f post_proc -b volteer brya nissa

    * Query by the specified feature 'post_proc' (post-processing) for the list
      of devices with support.
    * Specify the query pool among devices upon board 'volteer', 'brya', and
      'nissa'.

  feature_support.py -f -v

    * Query by the specified feature while the decision is deferred to the
      selection I/O later.
    * Run in verbose mode (may produce enormous messages from ucmlint and this).

  feature_support.py -o csv.txt --no-report-stdout

    * Query the full support matrix of features for all available devices.
    * Generate output file 'csv.txt' to save the support matrix in csv format.
    * Don't show the report on stdout.
"""

# const str for indicating feature selection I/O
SELECT_WITH_IO = 'tbd'


def match_hifi_modifier_name(key: str, hifi: ucmlint.HiFiParser) -> bool:
    """Matches key with the names of SectionModifier in HiFi.conf.

    Args:
        key: the key word in string for matching.
        hifi: the ucmlint.HiFiParser object.

    Returns:
        True if matches, otherwise False.
    """
    return any([mod.name == key for mod in hifi.modifiers])


def match_hifi_device_name(key: str, hifi: ucmlint.HiFiParser) -> bool:
    """Matches key with the names of SectionDevice in HiFi.conf.

    Args:
        key: the key word in string for matching.
        hifi: the ucmlint.HiFiParser object.

    Returns:
        True if matches, otherwise False.
    """
    return any([device.name == key for device in hifi.devices])


def match_hifi_playback_seq(key: str, hifi: ucmlint.HiFiParser) -> bool:
    """Matches key with EnableSequence of playback in HiFi.conf.

    The matching target includes the main section, and playback devices such as
    Speaker, Headphone, and HDMI.

    Args:
        key: the key word in string for matching.
        hifi: the ucmlint.HiFiParser object.

    Returns:
        True if matches, otherwise False.
    """
    if hifi.verb is None:
        return False  # empty HiFiConf

    for item in hifi.verb.enable_sequence.items:
        if item.lhs == 'cset-tlv' and key in item.rhs:
            return True

    applied_device_types = [
        ucmlint.DeviceType.Speaker,
        ucmlint.DeviceType.Headphone,
        ucmlint.DeviceType.HDMI,
    ]
    for device in hifi.devices:
        if ucmlint.DeviceType.fromstring(device.name) in applied_device_types:
            for item in device.enable_sequence.items:
                if item.lhs == 'cset-tlv' and key in item.rhs:
                    return True
    return False


def match_sound_card_amp(key: str, sound_card_amp: str) -> bool:
    """Matches key with the sound card amplifier name.

    Args:
        key: the key word in string for matching.
        sound_card_amp: the amplifier name in string from sound-card-init config
                        file; None when no config file is found.

    Returns:
        True if matches, otherwise False.
    """
    if sound_card_amp is None:
        return False
    return key in sound_card_amp


@dataclasses.dataclass
class MatchMethod:
    keys: list[str]  # the list of key words for matching
    func: function  # the applied matching function

    def is_matched(self, **kwargs) -> bool:
        """Checks if the input args can match.

        Args:
            kwargs may contain the following items which are available arguments
            requested by matching function:
                'hifi: ucmlint.HiFiParser'
                'sound_card_amp: str'

        Returns:
            True if at least one key word in keys is matched via func.
        """
        func_params = inspect.signature(self.func).parameters
        # get the subset of arguments required by func
        func_kwargs = dict((k, v) for k, v in kwargs.items() if k in func_params)
        return any([self.func(key, **func_kwargs) for key in self.keys])


@dataclasses.dataclass
class Feature:
    name: str  # the short name used for identification
    full_name: str  # the full name which is readable by users
    match_methods: list[MatchMethod]  # the list of sufficient matching methods

    def is_supported(self, hifi: ucmlint.HiFiParser, sound_card_amp: str) -> bool:
        """Whether this feature is supported.

        Args:
            hifi: the ucmlint.HiFiParser object.
            sound_card_amp: the amplifier name in string from sound-card-init config
                file; None when no config file is found.

        Returns:
            True if this feature is supported; False otherwise.
        """
        return any(
            [mm.is_matched(hifi=hifi, sound_card_amp=sound_card_amp) for mm in self.match_methods]
        )


FEATURES = [
    Feature(
        name='nc',
        full_name='DSP Noise Cancellation',
        match_methods=[
            MatchMethod(keys=['Internal Mic Noise Cancellation'], func=match_hifi_modifier_name),
        ],
    ),
    Feature(
        name='aec',
        full_name='DSP Acoustic Echo Cancellation',
        match_methods=[
            MatchMethod(keys=['RTC Proc Echo Cancellation'], func=match_hifi_modifier_name),
        ],
    ),
    Feature(
        name='waves',
        full_name='Post-processor - Waves',
        match_methods=[
            MatchMethod(keys=['waves_params'], func=match_hifi_playback_seq),
        ],
    ),
    Feature(
        name='dts',
        full_name='Post-processor - DTS',
        match_methods=[
            MatchMethod(keys=['dts_sdk'], func=match_hifi_playback_seq),
        ],
    ),
    Feature(
        name='cirrus',
        full_name='Smart Amplifier - Cirrus',
        match_methods=[
            # known Cirrus-supplied smart amp names and aliases so far
            MatchMethod(keys=['cs35l41'], func=match_sound_card_amp),
        ],
    ),
    Feature(
        name='dsm',
        full_name='Smart Amplifier - Maxim DSM',
        match_methods=[
            # known Maxim-supplied DSM smart amp names and aliases so far
            MatchMethod(
                keys=['max98373', 'max98390', 'm98390', 'r1011'], func=match_sound_card_amp
            ),
            MatchMethod(keys=['dsmparam'], func=match_hifi_playback_seq),
        ],
    ),
    Feature(
        name='hfp',
        full_name='Bluetooth Offload - HFP',
        match_methods=[
            # match device "SCO Line In" only. "SCO Line Out" can be skipped
            # because it is always paired with input.
            MatchMethod(keys=['SCO Line In'], func=match_hifi_device_name),
        ],
    ),
]


@dataclasses.dataclass
class GenericFeature:
    name: str  # the short name used for identification
    full_name: str  # the full name which is readable by users
    subfeatures: set  # the set of feature names belong to this

    def is_supported(self, support_features: set) -> bool:
        """Returns true if this feature is supported, false otherwise."""
        return any(support_features & self.subfeatures)


GENERIC_FEATURES = [
    GenericFeature(
        name='post_proc', full_name='Post-processor (any)', subfeatures={'waves', 'dts'}
    ),
    GenericFeature(
        name='smart_amp', full_name='Smart Amplifier (any)', subfeatures={'cirrus', 'dsm'}
    ),
    GenericFeature(name='bt_off', full_name='Bluetooth Offload (any)', subfeatures={'hfp'}),
]


ALL_FEATURES = FEATURES + GENERIC_FEATURES


def get_full_name(feature_name: str) -> str:
    """Gets the full name of feature from the short name."""
    for f in ALL_FEATURES:
        if f.name == feature_name:
            return f.full_name
    return 'Unknown Feature ({feature_name})'


@dataclasses.dataclass
class DeviceInfo:
    """The collected information of a device."""

    board: str
    path: pathlib.Path
    sound_card_amp: str


class FeatureSupportArgsError(Exception):
    """Error from the assigned arguments."""


class FeatureSupportParsingError(Exception):
    """Error on parsing the feature support."""


def raise_parsing_error(msg: str):
    """Raises FeatureSupportParsingError.

    Args:
        msg: the error message in string.

    Raises:
        FeatureSupportParsingError
    """
    raise FeatureSupportParsingError(msg)


def hifi_filename(info: DeviceInfo) -> str:
    """The coral board devices are special cases which name 'HiFi'."""
    return 'HiFi' if info.board == 'coral' else 'HiFi.conf'


def fetch_common_hifi_paths(device: str, info: DeviceInfo) -> list[PosixPath]:
    """Gets the possible paths of the common HiFi.conf file.

    Args:
        device: the device name in string.
        info: the DeviceInfo object for the device.

    Returns:
        The list of possible paths of the common HiFi.conf file.
    """
    # replace PosixPath('**/files/<device>/audio') to PosixPath('**/files/common/audio')
    common_audio_path = info.path.parent.parent / 'common/audio'
    if not common_audio_path.is_dir():
        return []

    # try to extract the sound card name from CRAS config files.
    cras_conf_paths = sorted(info.path.glob('cras-config/*'))
    prox_card_names = [p.stem for p in cras_conf_paths if p.suffix != 'ini']
    if len(prox_card_names) == 0:
        logging.warning(
            'Unable to fetch any HiFi.conf from either device '
            f'`{device}` or common. Leave it feature-unsupported.'
        )
        return []

    # try to identify the right path of the applied common HiFi.conf
    common_hifi_paths = []
    for card_name in prox_card_names:
        common_hifi_paths.extend(
            sorted(common_audio_path.glob(f'ucm-config/{card_name}*/' + hifi_filename(info)))
        )

    return common_hifi_paths


def get_sound_card_init_amp(audio_path: pathlib.Path) -> Optional[str]:
    """Gets the amplifier name from sound-card-init config filename.

    The sound-card-init config is a YAML-typed file which name contains the
    amplifier name, e.g. vell.CS35L41.yaml and sofmt8188m98390.yaml.

    Args:
        audio_path: the device audio path.

    Returns:
        The amplifier name extracted from the sound-card-init config filename,
        or None if the config file is not found.
    """
    # folders are named "sound-card-init-config" or "sound_card_init-config"
    sound_card_init_paths = sorted(audio_path.glob('sound[_-]card[_-]init-config/*.yaml'))
    if len(sound_card_init_paths) == 0:
        return None

    if len(sound_card_init_paths) > 1:
        logging.warning(
            'Expect 1 yaml file for sound-card-init config. '
            f'Found {len(sound_card_init_paths)} under {audio_path}.'
        )

    return sound_card_init_paths[0].stem.lower()


def get_support_feature_set(device: str, info: DeviceInfo, error: function) -> Optional[set]:
    """Gets the supporting feature set for the specified device.

    Args:
        device: the device name in string.
        info: the DeviceInfo object for the device.
        error: the function for reporting errors.

    Returns:
        A set consisting of supporting feature names, e.g. {'nc', 'aec', 'hfp'}.

    Raises:
        FeatureSupportParsingError: unexpected error is occurred while parsing,
            as long as the raise is included in the error function.
    """
    logging.info(f'Parsing device `{device}`')

    hifi_paths = sorted(info.path.glob('ucm-config/**/' + hifi_filename(info)))
    if len(hifi_paths) == 0:
        logging.info(
            f'Unable to fetch HiFi.conf from device `{device}`, '
            'try to fetch the common one instead...'
        )
        hifi_paths = fetch_common_hifi_paths(device, info)

    if len(hifi_paths) == 0:
        error(f'Unable to fetch HiFi.conf from either device `{device}` or common.')

    if len(hifi_paths) > 1:
        ucm_skews = [s.parent.name for s in hifi_paths]
        logging.warning(
            f'HiFi.conf skews found in device `{device}`: {ucm_skews}, '
            'feature support will report True if any skew supports that feature.'
        )

    support_feature_set = set()
    for hifi_path in hifi_paths:
        relpath = str(hifi_path)
        hifi = ucmlint.HiFiParser(
            hifi_path.read_text(encoding='utf-8').splitlines(), path=relpath
        ).parse()
        if hifi is None:
            error(f'Unable to parse file: {relpath}')
            continue

        support_feature_set.update(
            set([f.name for f in FEATURES if f.is_supported(hifi, info.sound_card_amp)])
        )

    if len(hifi_paths) == 0 and info.sound_card_amp:
        # create an empty hifi to let feature support functions run smoothly
        empty_hifi = ucmlint.HiFiConf(verb=None, devices=[], modifiers=[], path="")
        support_feature_set.update(
            set([f.name for f in FEATURES if f.is_supported(empty_hifi, info.sound_card_amp)])
        )

    for generic_feature in GENERIC_FEATURES:
        if generic_feature.is_supported(support_feature_set):
            support_feature_set.add(generic_feature.name)

    return support_feature_set


def get_devices_for_query(args) -> Optional[dict]:
    """Gets devices with the corresponding infos as candidates for query.

    Args:
        args: the ArgumentParser object.

    Returns:
        A dict mapping devices to the corresponding DeviceInfo objects for ones
        to be queried.

    Raises:
        FeatureSupportArgsError: unexpected error is occurred while getting
            device candidates.
    """
    # check overlays dir is valid
    base_path = pathlib.Path(args.overlays_dir).absolute()
    if not base_path.is_dir():
        raise FeatureSupportArgsError(f'Invalid overlays dirpath: {args.overlays_dir}')

    if args.board:
        # if board(s) are set to args.board, make sure all of them are valid.
        for board in args.board:
            board_path = base_path / (k.OVERLAY_ + board)
            if not board_path.is_dir():
                raise FeatureSupportArgsError(f'Invalid board name: {board}')
        boards = args.board
    else:
        # if args.board is not set, fetch all valid boards.
        board_paths = sorted(base_path.glob(k.OVERLAY_ + '*'))
        boards = []
        for board_path in board_paths:
            if not board_path.is_dir():
                logging.warning(f'Unexpected non-dir path: {board_path}')
                continue
            boards.append(board_path.name[len(k.OVERLAY_) :])

    device_info = {}
    for board in boards:
        # <base>/overlay-<board>/chromeos-base/chromeos-bsp-<board>/files
        device_root_path = (
            base_path / (k.OVERLAY_ + board) / k.CHROMEOS_BASE / (k.CHROMEOS_BSP_ + board) / k.FILES
        )
        if not device_root_path.is_dir():
            logging.info(f'Skipped board `{board}` due to invalid path: {device_root_path}')
            continue

        # try to match audio path with the non-unibuild pattern, i.e. device
        # name is equivalent to board name.
        audio_path = device_root_path / 'audio-config'
        if audio_path.is_dir():
            device = board
            sound_card_amp = get_sound_card_init_amp(audio_path)
            device_info[device] = DeviceInfo(board, audio_path, sound_card_amp)
            continue

        # try to match audio path with the unibuild pattern. Collect all device
        # names while traversing sub-folders for each reference board.
        audio_paths = sorted(device_root_path.glob('*/audio/'))
        for audio_path in audio_paths:
            if not audio_path.is_dir():
                logging.warning(f'Unexpected non-dir path: {audio_path}')
                continue
            # get <device> from <device_root_path>/<device>/audio
            device = audio_path.parent.name
            # filter out reserved words
            if device in ['common']:
                continue
            sound_card_amp = get_sound_card_init_amp(audio_path)
            device_info[device] = DeviceInfo(board, audio_path, sound_card_amp)

    if args.device:
        # if args.device is set, only return with the corresponding item.
        if args.device not in device_info:
            raise FeatureSupportArgsError(f'Unable to fetch device: {args.device}')
        return {args.device: device_info[args.device]}
    # if args.device is not set, return all collected items.
    return device_info


def log_device_list(board_feature_dict: dict):
    """Makes the log for listing the queried devices.

    Args:
        board_feature_dict: a two-level dict mapping boards and devices to the
            supporting feature set.
    """
    device_list_log = ''
    num_devices = 0
    for board, device_feature_dict in sorted(board_feature_dict.items()):
        devices = sorted(list(device_feature_dict.keys()))
        device_list_log += f'{board}({len(devices)}):[{", ".join(devices)}] '
        num_devices += len(devices)

    logging.info(
        f'Reported from {num_devices} device models ({len(board_feature_dict)} boards) in total:'
    )
    logging.info(device_list_log)


def write_csv_output(output_path: str, board_feature_dict: dict):
    """Writes feature support info matrix to file.

    Args:
        output_path: the output filepath.
        board_feature_dict: a two-level dict mapping boards and devices to the
            supporting feature set.
    """
    features = [f.name for f in ALL_FEATURES]
    header_row = ['Board', 'Device'] + [f.full_name for f in ALL_FEATURES]
    with open(output_path, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        # write header row
        writer.writerow(header_row)
        # write feature support info row per device
        for board, device_feature_dict in sorted(board_feature_dict.items()):
            for device, device_feature in sorted(device_feature_dict.items()):
                info_row = [(lambda f: '1' if f in device_feature else '0')(f) for f in features]
                writer.writerow([board, device] + info_row)


def report_unrecognized_amps(device_infos: dict, board_feature_dict: dict):
    """Reports the unrecognized sound card amps for devices if any.

    A device is considered to have unrecognized amp if sound_card_amp of
    DeviceInfo is non-empty, but 'smart_amp' is not in its support features.

    Args:
        device_infos: a dict mapping device names to DeviceInfo.
        board_feature_dict: a two-level dict mapping boards and devices to the
            supporting feature set.
    """
    for device, info in device_infos.items():
        if info.sound_card_amp is None:
            continue

        support_feature_set = board_feature_dict.get(info.board, {}).get(device, set())
        if 'smart_amp' in support_feature_set:
            continue

        print(
            f'\n[WARN] UNRECOGNIZED SMART AMP: DEVICE={device}, BOARD={info.board}, AMP={info.sound_card_amp}'
        )


def report_device(device: str, board: str, support_feature_set: set):
    """Shows the report for the specified device.

    Args:
        device: specified device name in string.
        board: board name in string.
        support_feature_set: the feature set supported by the device.
    """
    print('\nREPORT FOR DEVICE FEATURE SUPPORT:')
    print(f'  MODEL: {device}')
    print(f'  BOARD: {board}')
    print('  SUPPORT FEATURES:')
    for f in FEATURES:
        if f.name in support_feature_set:
            print(f'      {f.full_name}')
    print('END OF REPORT')


def report_feature(feature: Feature, board_feature_dict: dict):
    """Shows the report for the specified feature.

    Args:
        feature: the Feature object for specified feature.
        board_feature_dict: a two-level dict mapping boards and devices to the
            supporting feature set.
    """
    print(f'\nREPORT FOR FEATURE `{feature.full_name}`:')
    print('  BOARD       |    COVERAGE RATIO |DIST HISTOGRAM           | SUPPORT MODELS')
    print(' -------------|-------------------|-------------------------|----------------')
    for board, device_feature_dict in sorted(board_feature_dict.items()):
        num_devices = len(device_feature_dict)
        support_devices = sorted(
            [d for d, fset in device_feature_dict.items() if feature.name in fset]
        )
        ratio = (len(support_devices) / num_devices) * 100
        coverage_str = f'{ratio:3.2f}% ({len(support_devices):3d}/{num_devices:3d})'
        histogram = '*' * len(support_devices) + '.' * (num_devices - len(support_devices))
        if len(histogram) > 24:
            # clip to the max length
            histogram = histogram[:23] + '>'
        print(f'  {board:12s}|{coverage_str:>18s} |{histogram:24s} | {", ".join(support_devices)}')
    print('END OF REPORT')


def get_feature_from_io() -> Optional[str]:
    """Gets feature selected from prompt user I/O.

    Returns:
        The feature name in string.

    Raises:
        FeatureSupportArgsError: invalid user input is received.
    """
    print('\nAvailable features for query:')
    for index, feature in enumerate(ALL_FEATURES, 1):
        print(f'  ({index}) {feature.full_name}')

    user_input = input('Select a feature by entering the number: ')
    try:
        num = int(user_input)
    except ValueError:
        raise FeatureSupportArgsError(f'Invalid input on selection: {user_input}')

    if 0 < num < len(ALL_FEATURES) + 1:
        return ALL_FEATURES[num - 1].name
    raise FeatureSupportArgsError(f'Selection out of range: {user_input}')


def main():
    parser = argparse.ArgumentParser(
        description=PROG_DESCRIPTION,
        epilog=PROG_EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        '-d',
        '--device',
        type=str,
        help='device name for querying the list of supported features',
    )
    parser.add_argument(
        '-f',
        '--feature',
        nargs='?',
        help='feature name for querying the list of devices with support. If '
        'set without following argument, prompt I/O to select feature later',
        const=SELECT_WITH_IO,
        choices=[f.name for f in ALL_FEATURES] + [SELECT_WITH_IO],
    )
    parser.add_argument(
        '-b',
        '--board',
        nargs='*',
        help='reference board name(s) to be queried',
    )
    parser.add_argument(
        '-o',
        '--csv-output',
        type=str,
        help='output filepath to write the feature support table in CSV format',
    )
    parser.add_argument(
        '-l',
        '--overlays-dir',
        type=str,
        help='path of overlays dir (the root dir, i.e. $CROS_ROOT/src/overlays/), '
        'default is the relative path assuming pwd is the same location as this '
        'python script',
        default='../../../overlays/',
    )
    parser.add_argument(
        '-v',
        '--verbose',
        action='store_true',
        help='run in verbose mode (logging level set to INFO; ERROR in default)',
        default=False,
    )
    parser.add_argument(
        '--no-report-stdout',
        action='store_false',
        dest='report_stdout',
        help='run without printing report to stdout (requires "-o CSV_OUTPUT" along)',
    )
    parser.add_argument(
        '--stop-on-error',
        action='store_true',
        help='terminate once encountering parsing error',
    )

    args = parser.parse_args()

    loglevel = logging.INFO if args.verbose else logging.ERROR
    logging.basicConfig(level=loglevel, format='[%(levelname)8s] %(module)s: %(message)s')

    error_func = raise_parsing_error if args.stop_on_error else logging.error

    # "--no-report-stdout" requires "-o CSV_OUTPUT" along.
    if not args.report_stdout and not args.csv_output:
        raise FeatureSupportArgsError('"-o CSV_OUTPUT" is required along with "--no-report-stdout"')

    # honor "-d DEVICE" to provide feature support list once given.
    if args.device:
        args.board = None
        args.feature = None

    # when "-f" is set without following assignment, prompt the user selection I/O
    if args.feature == SELECT_WITH_IO:
        args.feature = get_feature_from_io()

    # device_infos: dict[str, DeviceInfo] with device names as keys
    device_infos = get_devices_for_query(args)

    # board_feature_dict: dict[str(board), dict[str(device), set]], a two-level
    # bucket categorized by board as the 1st key, device name as the 2nd.
    board_feature_dict = {}
    for device, info in device_infos.items():
        try:
            feature_set = get_support_feature_set(device, info, error_func)
        except Exception as e:
            e.add_note(f'While parsing device model `{device}`.')
            raise

        # update supporting feature set into board_feature_dict
        if info.board not in board_feature_dict:
            board_feature_dict[info.board] = {}
        board_feature_dict[info.board].update({device: feature_set})

    log_device_list(board_feature_dict)

    if args.csv_output:
        write_csv_output(args.csv_output, board_feature_dict)

    print(
        '\nNote: Tool may have inaccuracy in sparse cases. Please report to '
        'johnylin@ when you locate one.'
    )

    if not args.report_stdout:
        return

    if args.device:
        # report for device
        board = device_infos[args.device].board
        device_feature_dict = board_feature_dict.get(board)
        if (device_feature_dict is None) or (args.device not in device_feature_dict):
            raise FeatureSupportParsingError(
                f'Unable to parse feature support for device: {args.device}'
            )
        report_device(args.device, board, device_feature_dict[args.device])
        # report unrecognized sound card amp name if it is
        report_unrecognized_amps(device_infos, board_feature_dict)
        return

    # report for features
    for feature in ALL_FEATURES:
        # if args.feature is set, only report that feature
        if (args.feature is None) or (args.feature == feature.name):
            report_feature(feature, board_feature_dict)
            # report unrecognized sound card amp names if any
            if feature.name == 'smart_amp':
                report_unrecognized_amps(device_infos, board_feature_dict)


if __name__ == '__main__':
    main()
