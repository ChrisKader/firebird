# Firebird Web Qt

This wrapper project configures Firebird through the Qt WebAssembly toolchain.

It forces:

- `FIREBIRD_ENABLE_KDDOCKWIDGETS=ON`
- `FIREBIRD_KDDOCKWIDGETS_PREFIX=.build/deps/kddockwidgets-2.4-wasm`

Use through the meta target:

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_TARGETS=webqt
cmake --build .build/meta --target firebird-build-web-qt --parallel
```
