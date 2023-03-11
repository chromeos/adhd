# Ebuild Bazel http_archive dependency maintainer guide

## Introduction

[bazel.eclass] uses `bazel_external_uris` to pre-fetch `http_archive`s for
Bazel, so Bazel can continue run inside portage's network namespace.

This package help us maintain our `bazel_external_uris` list and upload
our packages to `gs://chromeos-localmirror`.

## Check mirror

Run:

```
bazel run //repositories/http_archive_deps:check_mirror
```

It tells you whether an `http_archive` is already mirrored, or prints the command
to upload them.

Example output:

```
# ⚠️  bazelbuild-bazel-skylib-1.0.3.tar.gz is not uploaded yet. Upload with:
gsutil cp -n -a public-read /usr/local/google/home/aaronyu/.cache/bazel/_bazel_aaronyu/cache/repos/v1/content_addressable/sha256/1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c/file gs://chromeos-localmirror/distfiles/bazelbuild-bazel-skylib-1.0.3.tar.gz
# ✅  google-benchmark-v1.5.5.tar.gz already exist at: https://storage.googleapis.com/chromeos-localmirror/distfiles/google-benchmark-v1.5.5.tar.gz
```

## Ebuild snippet

The `bazel_external_uris` snippet for [adhd-9999.ebuild] is at:

```
bazel-bin/repositories/http_archive_deps/bazel_external_uris.txt
```

It is also automatically updated when running the check mirror tool.

To only build the snippet, run:

```
bazel build //repositories/http_archive_deps:bazel_external_uris
```

To update the ebuild:

1.  Copy the relevant parts to `bazel_external_uris` in [adhd-9999.ebuild].
2.  In the SDK chroot, run:

    ```
    ebuild ~/chromiumos/src/third_party/chromiumos-overlay/media-sound/adhd/adhd-9999.ebuild manifest
    ```

[bazel.eclass]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/portage-stable/eclass/bazel.eclass
[adhd-9999.ebuild]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/chromiumos-overlay/media-sound/adhd/adhd-9999.ebuild

## Verify http_archive urls

Use:

```
bazel run //repositories/http_archive_deps:verify_urls
```

to verify all urls in `http_archive` rules are valid.
