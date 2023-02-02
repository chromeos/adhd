# Build

1.  git clone this repository
2.  `cd gg`
3.  `go build ./cmd/...`

# Install-only instructions

```
go install chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/bulkreview/cmd/...@latest
```

# Command Syntax

```
bulkreview chromium:12345 [--cq=#] [--cr=#] [--v=#] \
    [--follow-relation] [--follow-cq-depend] [--stalk]
```
