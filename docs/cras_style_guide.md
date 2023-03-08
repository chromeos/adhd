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

## Local Variable Declaration

Declare the local variable as close as the first use.

Avoid declaring a variable without giving it an initial value.

Please refer to https://google.github.io/styleguide/cppguide.html#Local_Variables.

NOTE: Migration of existing code is tracked by b/261929984.

## String Manipulation

For string manipulation, prefer to use Rust for new modules. If not possible,
prefer to use "safe" string functions that are bounded.

The following is an incomprehensive list of the "bad" functions and their
suggested alternatives.

*   `sprintf` -> `snprintf` or `scnprintf` if using the return value
*   `strcmp` -> `strncmp`
*   `strcpy` `strncpy` -> `strlcpy`
*   `strlen` -> `strnlen`

## Build Time Feature Switches

In the build system, `-DHAVE_COOL_FEATURE=1` when COOL_FEATURE is enabled,
`-DHAVE_COOL_FEATURE=0` when it is disabled.

In C code, use `#if HAVE_COOL_FEATURE` to conditionally compile code for
COOL_FEATURE.

Do not use `#ifdef` or `#ifndef` for feature control. `#ifdef` is error prone
because it cannot let the compiler ensure feature flags are properly passed
and cannot prevent typos.

Exception: platform _detection_ (`#ifdef __ANDROID__`) and language _detection_
(`#ifdef __cplusplus`) is allowed.

## Comments

### Struct Members Comments

Use your best judgement on whether your struct members need comments. When they
do, comment your struct members right above or next to their declaration,
instead of commenting all together above the struct declaration as a huge
comment block.

The benefits are:
1.  Harder for those comments to be outdated.
2.  Editor plugins like `clangd` recognize this comment style.


Good: right above declaration.

```
/*
 * Struct to hold current ramping action for user.
 */
struct cras_ramp_action {
  // See CRAS_RAMP_ACTION_TYPE.
  enum CRAS_RAMP_ACTION_TYPE type;
  // The initial scaler to be applied.
  float scaler;
  // The scaler increment that should be added to scaler for every frame.
  float increment;
  float target;
};
```

Good: next to declaration.

```
/*
 * Struct to hold current ramping action for user.
 */
struct cras_ramp_action {
  enum CRAS_RAMP_ACTION_TYPE type; // See CRAS_RAMP_ACTION_TYPE.
  float scaler; // The initial scaler to be applied.
  float increment; // The scaler increment that should be added to scaler for every frame.
  float target;
};
```

Bad: huge comment block.

```
/*
 * Struct to hold current ramping action for user.
 * Members:
 *   type: See CRAS_RAMP_ACTION_TYPE.
 *   scaler: The initial scaler to be applied.
 *   increment: The scaler increment that should be added to scaler for every
 *              frame.
 */
struct cras_ramp_action {
  enum CRAS_RAMP_ACTION_TYPE type;
  float scaler;
  float increment;
  float target;
};
```

### Enum Item Comments

Use your best judgement on whether your enum items need comments. When they
do, for the same reason as struct member comments,
comment your enum members right above or next to their declaration.

Good: next to declaration.

```
enum CRAS_CONNECTION_TYPE {
  CRAS_CONTROL, // For legacy client.
  CRAS_PLAYBACK, // For playback client.
  CRAS_CAPTURE, // For capture client.
  CRAS_NUM_CONN_TYPE,
};
```

Good: right above declaration.

```
enum CRAS_CONNECTION_TYPE {
  // For legacy client.
  CRAS_CONTROL,
  // For playback client.
  CRAS_PLAYBACK,
  // For capture client.
  CRAS_CAPTURE,
  CRAS_NUM_CONN_TYPE,
};
```

Bad: huge comment block.

```
/* CRAS_CONTROL - For legacy client.
 * CRAS_PLAYBACK - For playback client.
 * CRAS_CAPTURE - For capture client.
 */
enum CRAS_CONNECTION_TYPE {
  CRAS_CONTROL,
  CRAS_PLAYBACK,
  CRAS_CAPTURE,
  CRAS_NUM_CONN_TYPE,
};
```
