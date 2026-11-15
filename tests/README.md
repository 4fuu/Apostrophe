# Apostrophe regression checks

## Version macros (host)

`tests/version.c` checks the framework version macros and compile-time
compatibility assertions without opening a display:

```sh
make test-version
```

It verifies `AP_VERSION_REQUIRE()`, `AP_VERSION_AT_LEAST()`, the decimal
encoding, and that `AP_VERSION_STRING` matches the numeric components. This is
the host check for Apostrophe's build-time version; it does not test NextUI
system compatibility or `pak.json` `min_system`.

## Host adapter (host)

`tests/host_adapter.c` checks the optional `ap_host_*` seam with a fake backend
and a headless SDL instance:

```sh
make test-host
```

It covers detached no-ops, attach/detach idempotence, transition-only overlay
events, input yield/return ordering, queued SDL input isolation across an
overlay open/close, one-shot quit delivery, and `ap_host_present()` calling the
backend draw before presenting. The fake backend is test logic, not NextUI or a
system overlay. `make test` runs this check after `test-version`.

## Input regression checks (device toolchain)

Build both checks with the same universal toolchain used by the examples:

```sh
docker run --rm -v "$PWD":/workspace \
  ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab \
  sh -c 'for name in input h700_uinput; do
    make -C /workspace -f ports/tg5040/Makefile \
      PLATFORM_DEFINE=PLATFORM_NEXTUI EXAMPLE="$name" \
      SRC="/workspace/tests/$name.c" BUILD_DIR=/workspace/build/tests || exit
  done'
```

Copy `build/tests/input` to a supported NextUI device and run it with that
firmware's SDL libraries in `LD_LIBRARY_PATH`. It checks normal and flipped
face-button mappings across all four platform IDs, and mocks device discovery
to verify that H700 keeps its built-in controls and power key while rejecting
external controllers and failed name queries. It does not open a display.

`build/tests/h700_uinput` runs on H700 as root with `/dev/uinput`. Pause the
frontend while running it so the generated A presses cannot activate its menus.
The test creates a temporary USB-style controller, opens it through the real
SDL backend, and verifies one press/release, no navigation from its right stick,
and flipped A-to-B mapping. The virtual device disappears when the process exits.
Resume the frontend afterward, including if the test fails.
