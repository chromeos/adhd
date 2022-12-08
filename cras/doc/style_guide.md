# CRAS Style Guide

The purpose of this style guide is to achieve consistent style of code in CRAS.
Existing code may not adhere to the style listed here but please follow the
styles listed here for new code when possible.

[TOC]

## Proposing a new style

1.  Upload a CL on gerrit to add the new/changed style to our style guide. See
    the history of this document for examples.
2.  Add `chromeos-audio-sw@google.com` to the CC list of the CL and discuss on the CL.
3.  Get 2 reviewers to approve the change, then submit the CL.
    OR get 1 reviewer to approve the change and wait for 1 week, then submit.
    If there is no activity on the CL it is assumed that every one is happy with the new style.
4.  File a bug to track the clean up of existing code.
5.  If possible, add a presubmit check to prevent writing more legacy code.

The purpose of a written process is to:

1.  Allow seeking feedback on changes made across the code base in a timely manner.
2.  To amplify our code reviews.
    When the reviewer and the author argued, and eventually agreed on how code
    should be written, we should document it so that the agreed style is applied
    consistently to our code base.

## Error level

CRAS uses syslog for logging.

The levels are:

*   `LOG_EMERG` - unused.
*   `LOG_ALERT` - unused.
*   `LOG_CRIT` - unused.
*   `LOG_ERR` - programming errors, configuration errors such as invalid UCM
    and other exceptional errors such as OOM.
    Errors usually cause user action to fail.
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

NOTE: Migration of existing code is tracked by b/255656013.
