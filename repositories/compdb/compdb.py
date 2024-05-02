# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import shlex
import subprocess
import sys


def find_build_target(args):
    i = args.index('-c')
    return args[i + 1]


def run(directory, output_file, target, aquery_args):
    print('[compdb] Ensuring external/ symlink', file=sys.stderr)
    try:
        os.symlink('bazel-out/../../../external', os.path.join(directory, 'external'))
    except FileExistsError:
        pass

    cmd = [
        'bazel',
        'aquery',
        f'mnemonic("CppCompile", {target})',
        '--output=jsonproto',
        *aquery_args,
    ]
    print('[compdb] Running', shlex.join(cmd), file=sys.stderr)
    output = subprocess.check_output(cmd, cwd=directory)

    action_graph = json.loads(output)
    directory_json = json.dumps(directory)

    print('[compdb] Writing', output_file, file=sys.stderr)

    with open(output_file, 'w', encoding='utf-8') as file:
        file.write('[')

        for i, action in enumerate(action_graph['actions']):
            file.write(
                '''{maybe_comma}
  {{
    "file": {file},
    "arguments": {arguments},
    "directory": {directory}
  }}'''.format(
                    maybe_comma=(',' if i > 0 else ''),
                    file=json.dumps(find_build_target(action['arguments'])),
                    arguments=json.dumps(action['arguments']),
                    directory=directory_json,
                )
            )

        file.write('\n]\n')

    print('[compdb] Done', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('target', default='"//..."', nargs='?')
    parser.add_argument('--aquery-arg', action='append')
    args = parser.parse_args()

    directory = os.environ['BUILD_WORKSPACE_DIRECTORY']

    run(
        directory=directory,
        output_file=os.path.join(directory, 'compile_commands.json'),
        target=f'deps({args.target})',
        aquery_args=args.aquery_arg or [],
    )


if __name__ == '__main__':
    main()
