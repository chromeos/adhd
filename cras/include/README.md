# //cras/include

Everything in this directory is exposed to clients and very hard to
change once a CL lands, so think twice before changing things here.

This is because the client and the server may not be using the same source:

*   Chrome (ash): uses in-tree CRAS headers. However we cannot use Cq-Depend
    to make atomic API changes, but instead an upgrade window must be
    maintained when changing public APIs, because Chrome (ash) has
    a uprev process.
    *   Ash also sometimes references enum values defined in `cras_types.h`
        when dealing with D-Bus messages. Compatibility for those should be
        maintained too.
*   crosvm: uses in-tree CRAS headers. Similar to Chrome (ash), we cannot use
    Cq-Depend to make atomic API changes, but instead an upgrade window must be
    maintained when changing public APIs, because crosvm code are landed
    first in the upstream branch, then merged back to the chromeos-specific
    branch.
*   ARC++: is built using vendored cras source. Therefore when updating
    libcras, the *cras_client socket ABI must be compatible*,
    until the vendored cras source catches up.
