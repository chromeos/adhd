#!/usr/bin/python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# usage: merge_2way_conf [1st-way conf] [2nd-way conf]

"""

output files (in the working directory):
    audio-2way.conf - merged audio.conf file for 2way usage
    bandsplit-param-lines.txt - lines of code to be filled into tuning script

asserts:
  - global config variables are equivalent on both ways
  - drc parameters are equivalent on both ways
  - bandsplit filter parameters are equivalent on left and right channel
  - the number of EQ filters per channel after merged is <= 8
"""

import argparse
import fnmatch
import json


MERGED_CONF_OUT = 'audio-2way.conf'
BANDSPLIT_PARAMS_OUT = 'bandsplit-param-lines.txt'
BANDSPLIT_PARAM_CODE = {
    'highpass': 'eq.PEQ_HP2G {freq:d} NaN {q:.4f}',
    'lowpass': 'eq.PEQ_LP2G {freq:d} NaN {q:.4f}',
    'highshelf': 'eq.PEQ_HS2G {freq:d} {gain:.4f} NaN',
    'lowshelf': 'eq.PEQ_LS2G {freq:d} {gain:.4f} NaN',
}
NUM_WAYS = 2
NUM_CHANNELS = 2
NUM_FILTERS = 8


def filter_keys(d: dict, pattern: str) -> dict:
    return {k: v for k, v in d.items() if fnmatch.fnmatch(k, pattern)}


def is_identical(conf1, conf2, pattern: str) -> bool:
    """
    Checks if all items whose key is matched with pattern in conf1 and conf2 are
    equivalent. Returns False if no item key is matched.
    """
    return filter_keys(conf1, pattern) is not None and filter_keys(conf1, pattern) == filter_keys(
        conf2, pattern
    )


def copy_entries(conf_src, conf_dst, pattern: str):
    """
    Copies items whose key is matched with pattern in conf_src to conf_dst.
    """
    conf_dst.update(filter_keys(conf_src, pattern))


def copy_eq_entries_with_new_id(conf_src, conf_dst, pattern: str, new_id: int):
    """
    Copies eq-specific items whose key is matched with pattern in conf_src to
    conf_dst with modification to new id.
    """
    for k in filter_keys(conf_src, pattern):
        # modify the key to new id, where id stands for the latter number,
        # e.g. key='eq.0.2.enable' new_id=6 --> mod_key='eq.0.6.enable'
        k_split = k.split('.')
        k_split[2] = str(new_id)
        k_new = '.'.join(k_split)
        conf_dst[k_new] = conf_src[k]


def print_bandsplit_param_codes(params: dict) -> str:
    print_str = ''
    for code, num in params.items():
        print_str += f'                 {code} ; ...\n' * num
    return print_str


def merge_2way_conf(conf_file_1st, conf_file_2nd: str):
    # read and parse audio.conf files into confs[way]: dict
    confs = []
    with open(conf_file_1st) as f:
        confs.append(json.load(f))
    with open(conf_file_2nd) as f:
        confs.append(json.load(f))

    # conf_merged: dict
    conf_merged = {}

    # bandsplit_params[channel][way]: dict {
    #    key: str - bandsplit param code
    #    value: int - number of occurrences
    # }
    bandsplit_params = []

    # check global variables are aligned
    assert is_identical(confs[0], confs[1], 'global.*'), 'global vars not aligned'
    copy_entries(confs[0], conf_merged, 'global.*')

    # check drc params are equivalent
    assert is_identical(confs[0], confs[1], 'drc.*'), 'drc params not aligned'
    copy_entries(confs[0], conf_merged, 'drc.*')

    for ch in range(NUM_CHANNELS):  # channel - 0: left, 1: right
        bandsplit_params_per_ch = []
        bandsplit_params.append(bandsplit_params_per_ch)
        new_fid = 0

        for way in range(NUM_WAYS):  # way - 0: 1st-way, 1: 2nd-way
            bandsplit_params_per_ch_way = {}
            bandsplit_params_per_ch.append(bandsplit_params_per_ch_way)

            for fid in range(NUM_FILTERS):  # filter id - 8 in total
                eq_prefix = f'eq.{ch}.{fid}.'

                # helper function for getting eq parameter
                def _get_eq_param(param: str) -> any:
                    return confs[way][eq_prefix + param]

                if not _get_eq_param('enable'):
                    continue

                param_code = BANDSPLIT_PARAM_CODE.get(_get_eq_param('type'))
                if param_code is not None:
                    p = param_code.format(
                        freq=_get_eq_param('freq'), q=_get_eq_param('q'), gain=_get_eq_param('gain')
                    )
                    # store bandsplit_param in keys with value as the number of occurrences
                    bandsplit_params_per_ch_way[p] = bandsplit_params_per_ch_way.get(p, 0) + 1
                else:
                    copy_eq_entries_with_new_id(confs[way], conf_merged, eq_prefix + '*', new_fid)
                    new_fid += 1

        # check the number of enabled filters <= 8 after merge
        assert (
            new_fid <= NUM_FILTERS
        ), f'merged eq filter count {new_fid} > {NUM_FILTERS} on channel {ch}'
        # fill the rest as disabled
        for fid in range(new_fid, NUM_FILTERS):
            conf_merged[f'eq.{ch}.{fid}.enable'] = False

    # check the bandsplit filters are equivalent on left and right channel
    assert (
        bandsplit_params[0][0] == bandsplit_params[1][0]
    ), 'bandsplit params not aligned for 1st-way'
    assert (
        bandsplit_params[0][1] == bandsplit_params[1][1]
    ), 'bandsplit params not aligned for 2nd-way'

    # write output file: audio-2way.conf
    with open(MERGED_CONF_OUT, 'w') as f:
        json.dump(conf_merged, f, indent=2)
    print('generated output file:', MERGED_CONF_OUT)

    # write output file: bandsplit-param-lines.txt
    with open(BANDSPLIT_PARAMS_OUT, 'w') as f:
        f.write(
            f'''% To substitute the parameter set in lo_band_iir
eq.peq = [ ...
{print_bandsplit_param_codes(bandsplit_params[0][0])}
         ];

% To substitute the parameter set in hi_band_iir
eq.peq = [ ...
{print_bandsplit_param_codes(bandsplit_params[0][1])}
         ];
'''
        )
    print('generated output file:', BANDSPLIT_PARAMS_OUT)


def main():
    parser = argparse.ArgumentParser(
        epilog=__doc__,
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        'conf1',
        help='audio.conf file saved by WebUI for the 1st way tuning',
    )
    parser.add_argument(
        'conf2',
        help='audio.conf file saved by WebUI for the 2nd way tuning',
    )
    args = parser.parse_args()

    merge_2way_conf(args.conf1, args.conf2)


if __name__ == "__main__":
    main()
