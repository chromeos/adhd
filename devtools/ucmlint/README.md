# UCM Config Linter

This is an experimental UCM config linter for ChromeOS.

## Example Usage

### Lint a directory

```
./ucmlint.py ../../../../overlays/overlay-cherry/chromeos-base/chromeos-bsp-cherry/files/dojo/audio/ucm-config/sof-m8195_m98390_5682s.dojo/
```

### Lint a CL

```
./lint_cl.py 3769569
```

You can optionally specify `--work-dir=<path>` to let it re-use a git clone.

### Upload lint results to gerrit as draft comments

```
./draft_comments.py <output-of-lint-cl>.json
```

NOTE: `draft_comments.py` uses `~/.gitcookies` to access gerrit. Which are the
same credentials you use to perform `repo upload`.
