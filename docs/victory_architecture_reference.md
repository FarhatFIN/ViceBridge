# VIctory as an architectural reference

Reviewed 25 November 2026 at upstream commit `ffe310eb2098e52169f4c44db5ffdabf43a72622`:
https://github.com/Marvisak/VIctory/tree/ffe310eb2098e52169f4c44db5ffdabf43a72622

User constraint: reference architecture only. No VIctory implementation, constants, fixtures or assets are
transplanted into ViceBridge. The reference checkout is outside this repository. It was not built or executed.
Its README reports successful menus on Windows/AMD; that is an upstream claim, not our verified result on Wine/NVIDIA.

## Findings from implementation

- `victory-loader/src/windows.rs` separates mapping/import binding from execution. A dedicated title thread
  initializes dependent images, delivers title TLS callbacks and enters the title with an appropriate stack.
- `victory-loader/src/tls.rs` redirects executable TLS accesses to a separate image slot, preserving the host's
  thread-local environment. The applicable principle is isolation of host and title TLS, not reuse of its patcher.
- `victory-hle/src/modules/kernel.rs` uses one resolution policy for both imported and dynamically requested APIs.
  Host GetProcAddress overrides obtain the module name when needed instead of using our persistent name cache.
  That difference motivates a synthetic cache/TLS experiment; it does not prove our crash's root cause.
- `victory-gpu/src/texture.rs` associates title memory, native layout, staging footprints and a host texture.
  Changed source data is detiled before a submission uses it. Write tracking or content hashes avoid repeated
  uploads. GPU-written textures have separate ownership tracking so stale CPU data is not uploaded over them.
- `victory-hle/src/modules/audio.rs` separates the AcpHal interface, command/message state and a decoder worker.
  Completion records and notification writes are a distinct contract from decoded samples. Some commands are
  acknowledged without processing, explicitly documented there as shortcuts; that behavior is not an oracle.
- `victory-hle/src/modules/dsp.rs` documents silent output for unimplemented transforms. This does not establish
  correct reverb or justify claiming a complete DSP backend.

## Application to our own design

1. Reproduce the current host-module-name crash with a synthetic title that mixes static TLS, CRT initialization
   and dynamic import lookup; inspect host TLS and cache initialization before choosing a fix.
2. Define a native-texture source record with memory range, format/layout, subresources, dirty generation and
   CPU/GPU ownership. Confirm where the Xbox ABI supplies those values before connecting our compact detiler.
3. Test command acceptance, bounded completion queues, notification lifetime and shutdown separately from audio
   decoding. Independently establish method shapes and message fields before implementing them.
4. Keep portable layout tests and real GPU readback checks as separate gates. Successful synthetic rendering
   remains distinct from an authorized title startup and from correct game rendering.

The reference uses host D3D12 and DXIL; ViceBridge uses Vulkan and SPIR-V. Its host GPU objects or shader backend
cannot be treated as directly applicable to this runtime.

## Comparison with current public loaders

Reviewed the open loader and TLS sources at these revisions. The references were read only.

- [VIctory 940d32f, Windows loader](https://github.com/Marvisak/VIctory/blob/940d32f69b22265690a0b5a15d6ded572ba33baf/crates/victory-loader/src/windows.rs):
  dependent DLL process attach, EXE process TLS callbacks, and EXE entry run on the same dedicated thread.
  The upstream README reports menus and early world rendering with significant graphics defects.
- [XCL 1a0bbe3, loader](https://github.com/xcl-dev/xcl/blob/1a0bbe3a2ba91ea8f5b6fc29130a053868d3c9e6/crates/xcl/src/loader.rs):
  EXE process TLS callbacks run before the entry thread is created. Its thread notification handler
  prepares TLS before it calls mapped callbacks and initialized DLL entry points.
  The README reports a newer retail title reaching menus and early world rendering on its NVIDIA branch,
  without naming that title in the status row.

ViceBridge already prepares every active module's TLS block before it calls any thread attach callback.
It still runs process attach on its loader thread and enters the EXE on a fresh thread. The two references
differ on this timing, so the public code does not justify changing it on its own. An independent test
should record thread IDs and TLS values in DLL process attach, EXE process attach, EXE thread attach,
and the entry point, then run the same synthetic fixture under native Windows, native Wine, and
ViceBridge. Use those observations to decide whether and where to change ViceBridge. This test would
establish startup semantics, not attribute the current invalid guest pointer to TLS.
