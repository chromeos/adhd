# S2 - CRAS State v2

S2 serves as the replacement of cras_system_state and the central resolver
of audio effect decisions. S2 offers the following benefits:

1.  Make CRAS's state debuggable. All states defined in S2 can be dumped
    with a command line. This makes it easier to figure out what's going on
    in integration tests and logs.
2.  Make CRAS's state predictable. S2 should contain all the information needed
    to make decisions for CRAS. Which stream should have which effects, which
    device should be opened with which parameters could be all resolved in S2,
    so all the decisions can be unit tested.

See b/311612848 for more background.

## Debugging the live state

Run the following command on the DUT.

```
dbus-send --system --print-reply --dest=org.chromium.cras /org/chromium/cras org.chromium.cras.Control.DumpS2AsJSON
```
