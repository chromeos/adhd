#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script collects all dependencies specified in Cargo.toml files

[VPYTHON:BEGIN]
python_version: "3.8"

wheel: <
  name: "infra/python/wheels/semantic-version-py2_py3"
  version: "version:2.10.0"
>
wheel: <
  name: "infra/python/wheels/toml-py3"
  version: "version:0.10.2"
>
[VPYTHON:END]
"""

import dataclasses
from pathlib import Path
from typing import Set

import semantic_version
import toml


ADHD_DIR = Path(__file__).absolute().parent.parent


@dataclasses.dataclass
class Spec:
    version: semantic_version.Version
    features: Set[str]

    @classmethod
    def parse(cls, obj):
        if isinstance(obj, str):
            return cls(version=semantic_version.Version.coerce(obj), features=set())
        return cls(
            version=semantic_version.Version.coerce(obj['version']),
            features=set(obj.get('features', ())),
        )

    def merge(self, other: 'Spec'):
        return Spec(max(self.version, other.version), features=self.features | other.features)

    def render(self):
        version = f'"{self.version}"'
        if self.features:
            features = ', '.join(f'"{f}"' for f in self.features)
            return f'{{ version = {version}, features = [{features}] }}'
        return version


specs = {}

for path in ADHD_DIR.glob('**/Cargo.toml'):
    with open(path, encoding='utf-8') as file:
        manifest = toml.load(path)
        for section, items in manifest.items():
            # For [workspace.dependencies].
            if section == 'workspace' and 'dependencies' in items:
                section = 'dependencies'
                items = items['dependencies']

            # Merge all [dependencies], [dev-dependencies], etc.
            # For the purposes of src/third_party/rust_crates vendoring, the differences
            # between the dependency types do not matter.
            if section.endswith('dependencies'):
                for name, spec in items.items():
                    if spec == '*' or (
                        isinstance(spec, dict)
                        and (spec.get('workspace') or spec.get('version') == '*' or 'path' in spec)
                    ):
                        continue
                    s = Spec.parse(spec)
                    specs[name] = specs.get(name, s).merge(s)

print('# update this to src/third_party/rust_crates/projects/third_party/adhd-deps/Cargo.toml')
print('[dependencies]')
for name, spec in sorted(specs.items()):
    print(f'{name} = {spec.render()}')
