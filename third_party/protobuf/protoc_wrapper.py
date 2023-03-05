#!/usr/bin/env python3

import os
import sys


WELL_KNOWN_TYPES = [
    # Add more here when we use more protobuf well known types.
    'google/protobuf/any.proto',
]


args = sys.argv[1:]

# Inject --direct_dependencies.
# System protobuf already provides the well known types [1],
# and we let our proto_library to not explicitly declare the usage of
# the well known types. Implicitly declare them to keep protoc happy.
# [1]: https://protobuf.dev/reference/protobuf/google.protobuf/
try:
    i = args.index('--direct_dependencies')
except ValueError:
    args.append('--direct_dependencies')
    args.append(':'.join(WELL_KNOWN_TYPES))
else:
    args[i + 1] += ':' + ':'.join(WELL_KNOWN_TYPES)

os.execvp('protoc', ['protoc', *args])
