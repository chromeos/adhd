1. Copy
```
cras_audio_format.h
cras_iodev_info.h
cras_messages.h
cras_types.h
```

From cras/src/common from commit
5fd5e32c111ad28da0bb860d023b281ae16c2094

to `c_headers/`

2. From `rustc` error message:
```
E0587, // type has conflicting packed and align representation hints.
```
we can't use both `packed` and `align(4)` for a structure.

Since `cras_server_state` is created from C with `packed` and `aligned(4)`
and shared through shared memory area and structure with `packed` and `align(4)`
have the same memory layout compared to the one with `packed` except
for some extra aligned bytes in the end, using only `packed` for
`cras_server_state` from Rust side is safe.

Modify `cras_server_state` from
`__attribute__ ((packed, aligned(4)))`
to
`__attribute__ ((packed))`

3. And use command
```
cargo run
```

to generate `gen.rs`

4. Copy `gen.rs` to
`cras-sys/src/gen.rs
