# inline_member_comments

This tool moves struct member comments from the top block to the struct body.

## Run

```
cd ~/path/to/adhd/devtools
inline_member_comments/run.bash -p /absolute/path/to/cras /absolute/path/to/c/source.c
```

For an example what this tool changes please see https://crrev.com/c/4222575/1.


### Example

```
cd ~/chromium/src/third_party/adhd/cras
bazel run //:compdb
cd ~/chromium/src/third_party/adhd/devtools
inline_member_comments/run.bash -p $PWD/../cras $PWD/../cras/src/server/cras_iodev_list.c
```
