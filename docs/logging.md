# Logging

CRAS uses syslog for logging.

The levels are:

*   `LOG_EMERG` - unused.
*   `LOG_ALERT` - unused.
*   `LOG_CRIT` - unused.
*   `LOG_ERR` - programming errors, configuration errors such as invalid UCM and
    other exceptional errors such as OOM.
*   `LOG_WARNING` - transient errors, external errors and other errors that
    don't fit into `LOG_ERR`. Note that warnings can lead to errors,
    but a warning alone should not cause a bail out.
*   `LOG_NOTICE` - unused.
*   `LOG_INFO` - informational messages useful for debugging.
*   `LOG_DEBUG` - lower level informational messages useful for debugging.

By default, messages with level >= `LOG_WARNING` are recorded to `/var/log/messages`.
This can be overridden in `/etc/init/cras.override` with `--syslog_mask`.

For example to enable logging of all levels:

```
echo "env CRAS_ARGS='--syslog_mask 7'" >> /etc/init/cras.override
stop cras
start cras
```

Messages with level >= `LOG_ERR` are sent to [crash-reporter].

[crash-reporter]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/crash-reporter/README.md
