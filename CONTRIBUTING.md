# Contributing to VoltChanger

Use Windows x64, Rust MSVC and Visual Studio with C++/Windows SDK/CMake.
See the [README](README.md) for building and [technical notes](docs/TECHNICAL.md)
for the native module's architecture and persistent data.

Before submitting a change:

```powershell
cargo fmt --check
cargo clippy --locked --all-targets -- -D warnings
cargo test --locked
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

For native regression tests, build the CMake targets ending in `_tests` and run
`ctest --test-dir build/native -C Release --output-on-failure`.

Describe the behavior changed, how it was checked and any remaining limitations.
Never commit local inventories, configuration files, credentials, crash dumps,
personal match history, generated builds or an installed game's assets.
Keep original copyright and license notices intact.
