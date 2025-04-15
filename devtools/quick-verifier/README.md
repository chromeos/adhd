## Submit the builder for kythe

In the infra/cop directory https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:infra/cop/:

```
gcloud --project=chromeos-audio-kythe builds submit docker/adhd-archlinux-builder/ --tag gcr.io/chromeos-audio-kythe/adhd-kzip-builder
```

For other kythe-related operations refer to `chromeos-audio/infra/kythe` in internal docs.
