# Rust usage in CRAS

The two major Rust usage in CRAS are:

1.  Rust code used by CRAS server.
2.  Rust version of CRAS client.

## CRAS server Rust

Server Rust code is scattered around this repository.
The Cargo workspace is at the top directory `//`.
You can see the packages in the `workspace.members` entry in `Cargo.toml`.
New packages should be put into `//cras/server/<package-name>`.

Server Rust code is built with the `media-sound/cras_rust` package and then
used by the CRAS server in the `media-sound/adhd` package. To build and test your
code changes on a DUT you should `cros workon`, build and deploy both packages.

To build/test out of the chroot, run `cargo build --workspace` or `cargo test
--workspace` at the top directory `//`.

Bazel is also supported with `bazel test //...`, this allows you to unit test server
Rust and server C code at once.

If you get `cargo` to work, `rust-analyzer` should work as well.

When modifying Cargo.toml files for server Rust code, you also need to run
`CARGO_BAZEL_REPIN=true bazel sync --only=crate_index` to update the lockfiles
to keep Bazel working.

## CRAS client Rust

Client Rust code is located in the `//cras/client` directory, which is also the
Cargo workspace.

Client Rust code is built with the `media-sound/cras-client` package.

To build/test out of the chroot, set up tools and deps with:
*   `cargo install dbus-codegen`
*   `rustup default nightly`
*   `emerge-amd64-generic -j --onlydeps cras-client`
then you can run `cargo build --worspace` or `cargo test --workspace`
at the workspace directory `//cras/client`.

## Common server & client Rust

Common Rust code is located in the `//cras/common` directory.
The code there should be buildable for the server and client.
