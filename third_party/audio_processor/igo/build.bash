set -x
toolchain/bin/x86_64-cros-linux-gnu-clang++ \
    --sysroot=$PWD/sysroot \
    -Wl,--strip-all -Wl,-no-undefined -z defs -lc++ -lm \
    -Wl,--version-script=igo.lds \
    '-Wl,-rpath,$ORIGIN' \
    -std=gnu++20 -stdlib=libc++ -shared -fPIC -O2 igo_plugin.cc -o libigo_processor.so -l:libigo.a -l:libdnnl.so.1 -L.
