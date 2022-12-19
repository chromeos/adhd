# Tools to support bazel migration.

Run:

```
cd ~/path/to/adhd/cras
./git_prepare.sh
./configure CC=clang CXX=clang++
intercept -- make check TESTS= -j128  # this generates events.json
cd ~/path/to/adhd/bazel-migration-utils
go run .
```

`intercept` is a tool from [bear](https://github.com/rizsotto/Bear).
