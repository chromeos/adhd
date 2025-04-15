# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Fixes compilation_database.json so each output file (-o) appears only once.
"""

import collections
from dataclasses import asdict
from dataclasses import dataclass
import json


@dataclass(eq=True, frozen=True)
class Invocation:
    file: str
    arguments: tuple[str]
    directory: str

    def output(self):
        return self.arguments[self.arguments.index('-o') + 1]

    def key(self):
        return (
            not self.file.startswith('/usr'),
            not self.file.endswith('.h'),
            self.arguments,
            self.file,
            self.directory,
        )


populations = collections.defaultdict(set)

COMPDB = 'compile_commands.json'
with open(COMPDB) as file:
    data = json.load(file)

for raw_invocation in data:
    raw_invocation['arguments'] = tuple(raw_invocation['arguments'])
    invocation = Invocation(**raw_invocation)

    populations[invocation.output()].add(invocation)


out_data = []


for output, invocations in populations.items():
    out_data.append(asdict(max((invocation.key(), invocation) for invocation in invocations)[-1]))


with open(COMPDB, 'w') as file:
    json.dump(out_data, file)


print('Fixup: {len(data)} -> {len(out_data)} entries')
