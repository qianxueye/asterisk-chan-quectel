# Audio regression tests

On Linux, install a C compiler, Python 3, and ALSA development files, then run:

```sh
python3 test/test_pcm.py
python3 test/test_audio_lifecycle.py
```

`test_pcm.py` compiles the production `src/pcm.c` with test-only Asterisk stubs
and deterministic ALSA hardware responses, while retaining the real ALSA public
function declarations. It tests stream recovery and initialization failures,
including negotiated buffer geometry. A second executable uses real ALSA
`null` devices to check capture-before-playback startup, independent stream
states and 20 repeated restart cycles without physical audio hardware.

`test_audio_lifecycle.py` extracts the current production lifecycle and media
functions and compiles them against deterministic fake channels and PCM devices.
The harness asserts observable call transitions and sample delivery. It does not
maintain separate copies of the production algorithms.

The `CC` environment variable selects the compiler. Neither runner loads an
Asterisk module, opens a physical sound card, sends AT commands, or starts a call.
These tests also run through CTest for native builds with `BUILD_TESTING=ON`.
Cross-compiled builds retain the existing binary metadata checks but do not try
to execute target-architecture regression binaries on the build host.

For a full module build, supply headers matching the intended Asterisk ABI:

```sh
cmake --preset default -S . -B build -G Ninja \
  -DBUILD_TESTING=ON -DASTERISK_VERSION_NUM=220000
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The CI workflow prepares official Asterisk 22.0.0 headers and checks the complete
module in addition to the audio regressions. Hardware call validation remains
necessary; see [UAC recovery](../doc/uac-recovery.md).

The full CMake build also runs `Enum bounds regression` against the actual
Asterisk headers. It protects startup status queries from eager `S_COR` argument
evaluation with an unknown registration state; assertions remain enabled in
Release builds.
