# Progress

2026-11-25: synthetic graphics and compact texture paths pass targeted checks; a GPU readback was captured. An authorized game launch stopped at a null read in host GetProcAddress processing. See STATUS.md and TEST_RESULTS.md. Next: reproduce the host cache failure in an isolated synthetic TLS fixture.

2026-11-25: synthetic mapped EXE now resolves a host export from its CRT global constructor before main. run_tiny_gdk_load passed under Wine. Together with the fresh-thread xgameruntime test, this rules out a simple first lookup or early constructor as a sufficient trigger; title-specific TLS/CRT state remains under investigation. No loader fix claimed.

Host lookup follow-up: disassembly of the failing loader shows a TLS-backed static initialization guard immediately before a null cache-pointer dereference. Removed the `HMODULE` name cache because handle reuse could select stale shims and fresh lookups need no cache initialization. The underlying guard state is still unexplained. The mapped EXE fixture now writes guest TLS during its CRT constructor before resolving a host export and checks the value in main. Both host lookup suites passed under the same Wine prefix as the previous title attempt. The supplied package was identified as a leak. Runtime verification of the actual game requires a lawfully obtained package as specified in the project instructions.

Startup traces now record the assigned TLS index and template size (including zero-fill) on the module map event. startup_trace and run_tiny_gdk_load passed. A traced synthetic run under the title attempt’s Wine prefix reported TLS index 2 and 8 bytes and emitted its PASS marker. The prior title trace has no TLS metadata, so it cannot establish whether the guest accesses its assigned slot.

## Current checkpoint

- Goal: reproduce and capture a first frame rendered by the GTA VI engine using components
  authorized for this use. A synthetic frame is a regression check only.
- Loader baseline: 12 targeted Wine tests passed, covering mapped PE bounds, initialization,
  cross-module TLS, XMem, startup traces, indirect compute, compact texture upload, Vulkan
  presentation, and a mapped synthetic EXE rendering its first frame.
- ASM compatibility: an explicitly selected TLS access can use any slot assigned by the
  current loader (0–63). The short RAX form remains in-place for small slots. Other cases use
  a long-displacement trampoline. The synthetic tests execute the trampoline on a real slot
  and separately verify the encoding for slot 63. No game code or assets were used this cycle.
- Blocker for the title run: the only previously identified local package is from the leak.
  It is excluded. No suitable authorized package was identified in the workspace or the local
  game directory. A lawful package and a way to use it with the loader without bypassing access
  controls are needed before validating the real OEP, graphics initialization, or frame.
- Once available: run it offline with tracing, distinguish the first unimplemented API or crash
  from prior leak-specific observations, add a synthetic reproduction, then fix and retest.

## Loader and Vulkan regression pass

- The PE loader now propagates failed page protection changes and checks the effective page protection before dispatching OEP or TLS callbacks. The two new OEP cases reproduced acceptance of non-executable header and data addresses on the old loader. The callback case verifies that a module with a callback in the headers fails initialization.
- The mapped FLS fixture runs through both launcher entry modes. It checks that parent and child fibers on the same OS thread have separate FLS values and share static TLS, and that fiber deletion and slot release invoke the corresponding guest callbacks.
- Optional D3D12 tracing records the window presenter results for texture, composed and blank frames. The window, Xbox presentation core and mapped Vulkan render tests passed. The display exposed no HDR10 swapchain format. The Xbox PresentX guest vtable ABI remains unverified.
- The RGB9E5 compute round-trip failed under Wine when opening a Linux absolute path embedded by CMake. It now uses the shared shader fixture resolver. The bindless GPU write and sampled read matched all 16 HDR texels on the GTX 1650.
- Runtime traces, build output and the test transcript are appended to the local main.log. The pending title run still requires a lawfully available package. No title frame or title-specific OEP/ABI validation was obtained during this pass.

Combined Wine verification: all 19 selected PE, startup, shim and Vulkan tests passed.

## TuxBox interoperability review

- Reviewed public [brcly/TuxBox at 7b22d717](https://github.com/brcly/TuxBox/tree/7b22d7172e4eab0402ec61b56d6c9423f7abb5c5) under Apache-2.0. Its launcher uses the Windows PE loader under Wine/Proton for imports, TLS and unwind. Rust Xbox API libraries supply services and XMem. The graphics provider wraps desktop D3D12, translates RDNA2 through HLSL/DXC, and copies a single SDR plane to a DXGI swapchain. ViceBridge retains its existing PE mapper, ABI proxies and direct Vulkan/SPIR-V backend.
- TuxBox reports a displayed player-selection prompt. The later intro run ended with fence stalls and descriptor exhaustion. The prologue cutscene and image correctness were unverified. No game artifacts or TuxBox implementation source were imported.
- Implemented the published single-plane PresentX record layout independently in C++. The 72-byte plane and optional 32-byte parameter record feed ViceBridge's Vulkan presenter. The synthetic test checks actual swapchain pixels and rejects invalid counts, out-of-range thresholds and unknown options. TuxBox distinguishes queue slots 30 and 32 by profile, with other queue methods shifted too. Selecting an ABI profile and attaching a presenter to a guest queue remain open.

## Xbox package startup

- The user identified the local Scarlett 1.0.2.1 package as extracted from their Xbox. Launched it under Wine in a separate network namespace with a read-only package and a temporary write overlay.
- ViceBridge mapped five modules, reported five unresolved imports, ran DLL process attach and invoked two EXE TLS callbacks. Execution reached XMem calls, then Wine reported a read access violation at game_scarlett_master.exe+0x104078 (address 0xffffffffffffffff). No D3D12 trace or game frame was observed. The trace is in /tmp/vicebridge-offline.qlPI3d/startup.jsonl and the transcript is in main.log.

## Repeated GPU and loader verification

- Ran seven Wine fixtures four times each: loader TLS, FLS on entry, window presentation, Xbox presentation, translated draw, compact upload and mapped EXE first frame. All 28 runs passed.
- Tightened the mapped first-frame fixture: reject a footprint outside the readback allocation, stop before mapping on a failed signal or wait, and reject the device-removal fence sentinel. The rebuilt fixture passed four further runs, checking all 32 GPU pixels.
- Fixed DXR test ordering so valid BLAS/TLAS prebuild is checked before allocating acceleration-structure buffers. Forced KHR now reaches its capability skip on this GTX 1650. Default mode records a successful GPU BLAS/TLAS build and strict validation, but the first run timed out after that success message. A serial rerun with a 65-second limit passed the default case in 10.96 seconds and returned the expected KHR skip in 10.32 seconds.
- The offline game trace still ends in an indirect-call read fault before graphics. The five unresolved imports are Chad DSP APIs. The trace does not establish causation between those imports and the fault. No launch.log was found in the checkout.
- Startup trace regression: an existing trace caused open() to fail during the broader loader run. The fixture now reserves a fresh directory atomically, even when Wine reuses a PID. A regression check reserves two distinct directories in one process. The rebuilt test passed four consecutive runs. The other six checks in that loader run passed.

## OEP, hard-coded TLS and XErrorReport

- The startup trace now records the guest OEP and OS thread immediately before dispatch. Both startup modes check the event and verify that failed process attach never records entry. Startup, XMem, mapped exception and synthetic Vulkan frame tests passed.
- The offline baseline confirmed OEP at +0x56a1ec8 and assigned static TLS slot 2. Disassembly traced the original launch fault to a consumer using a hard-coded slot-zero read at +0x3a40. Redirecting that verified read with the existing in-memory ASM hook moved startup to a runtime initialization breakpoint, alongside a failed lookup of XErrorReport.
- Registered the documented XErrorReport export with the existing offline implementation and shared callback. The export, mapped GDK load and ASM hook tests passed. The next offline run passed the breakpoint and exposed another slot-zero consumer. Subsequent runs with individually verified sites +0x4ec674b, +0x4ec677a and +0x51d1ac4 reached a new fault at +0x3afc. No graphics trace or title frame yet.
- Latest offline trace and transcript: /tmp/vicebridge-tls-four.N0UufU/startup.jsonl and launch.log alongside it. The package was mounted read-only, with separate write overlays and network isolation.
- Verified another slot-zero read at +0x3ae4 in the +0x3afc fault path. Adding that fifth explicit site moved startup to +0x4ec668f. The current checkpoint is /tmp/vicebridge-tls-five.mYSgg4/startup.jsonl with launch.log in the same directory.

## TLS startup follow-up

- Confirmed the +0x4ec668f fault reads a count from an object obtained through a hard-coded slot-zero access at +0x4ec6675. Redirecting it exposed a second path to the original allocator dispatch, through another slot-zero access at +0x551d284.
- With both additional verified sites redirected, startup reached InitializeApiImplEx2 and then failed inside the host XTaskQueueCreate. PDB symbolization identified gta6rt.exe+0xe15a2 as TaskQueueImpl::Initialize (TaskQueue.cpp:2340), an indirect call through a null vtable. The cause of the host object's invalid state remains unverified. No graphics calls recorded.
- Added an executable regression for the mixed RAX/RCX TLS hook: it must preserve the TLS vector in RAX for subsequent indexed access. The ASM hook, loader TLS and mapped Vulkan-frame tests all passed.
- Latest isolated trace: /tmp/vicebridge-tls-seven.8P3Wul/startup.jsonl (launch.log beside it).

## Task queue and extended TLS destination

- Added start/result task-queue tracing for both the export and IXThreadingImpl. The mapped GDK fixture now exercises all 16 work/completion mode pairs, and the external check requires all 16 successful results. The mapped fixture and xgameruntime tests passed.
- Offline startup now has a confirmed successful ThreadPool/ThreadPool XTaskQueueCreate (HRESULT 0). An earlier host crash in the same vicinity remains unexplained and was not reproduced by the standalone fixture. Added REX.R support to the explicit TLS rewrite for an extended destination, with an executable R10 regression. The ASM and mapped GDK tests passed.
- Retested with the verified R13 load at +0x4fc12e4 redirected. Startup produced a secondary-thread access violation at +0x256f78c and reached the main-thread fault at +0x12d8863. The latter uses a slot-zero read separated from the TLS-vector load by a RIP-relative instruction, outside the current adjacent-pair rewrite. No D3D12 calls or game frame observed. Latest trace: /tmp/vicebridge-rex-tls.Lbk92W/startup.jsonl.

## Separated TLS reads

- Extended the opt-in TLS trampoline to accept one RIP-relative MOV between the TLS-vector load and slot load. The trampoline preserves the original memory operand by relocating its displacement, checks rel32 range, and refuses truncated sequences or an intervening write to the vector register.
- The executable regression verifies that the relocated instruction returns the original operand and the TLS read uses the assigned slot. ASM, mapped GDK and synthetic Vulkan frame tests passed.
- With the separated read and two worker-thread TLS reads redirected, the title advanced beyond the prior access violations. Another verified R13 destination read was then redirected. Latest launch still creates its task queue successfully, but stops on access violation +0x5a7de76 and records a worker-thread breakpoint at +0x3bf4. No D3D12 calls or title frame. Trace: /tmp/vicebridge-rip-followup.lzr8MI/startup.jsonl.

## TLS register-copy sequence

- Added support for a register copy before the intervening RIP-relative load, with checks against writes to the TLS vector or stack/frame registers. The executable regression verifies the copy, relocation and assigned-slot read, and rejects the truncated sequence. Build, ASM and synthetic GPU frame tests passed.
- Retested the verified sites in an isolated offline launch. The previous +0x5a7de76 and +0x57e1578 exceptions were passed; the latest main-thread failure is +0x406563b, alongside a worker breakpoint at +0x22106e0. Queue creation returned S_OK. No game frame yet.
- Latest trace /tmp/vicebridge-tls-next.z5Pv4b/startup.jsonl. Reproduction uses --attach-on-entry and explicit --tls-zero-read RVAs 3a40, 4ec674b, 4ec677a, 51d1ac4, 3ae4, 4ec6675, 551d284, 4fc12e4, 12d883e, 256f780, 256f796, 554ae4a, 5a7de4e and 57e1560.

## GameInput startup and scheduled TLS loads

- Redirecting the verified TLS read at +0x4065613 advanced the title to GameInputCreate. PDB and host disassembly located two crashes in succession at first use of function-local singleton pointers. Both paths used the MSVC TLS-backed initialization guard and returned null. Moved the state and COM singleton to host CRT initialization, preserving process lifetime. This avoids the affected guards in GameInput while the source of their anomalous state remains under investigation.
- The title then reached +0x53eb4ea. Added strict handling for the observed scheduled TLS sequence (stack stores, a RIP-relative MOV and a register widening operation). The executable test verifies truncation rejection, the assigned TLS pointer and the relocated operand. The title passed that fault after adding the site +0x53eb4b7; the last isolated launch stopped at the guest allocator failure breakpoint +0x22106e0. No breakpoint suppression was used.
- Reproduction helper for this session: /tmp/vicebridge-checkpoint-run.sh 53eb4b7. Trace before breakpoint instrumentation: /tmp/vicebridge-checkpoint.xTbKVr/startup.jsonl.
- Breakpoint trace now records thread and registers and has a mapped SEH regression. Startup trace, on-entry, GameInput, synthetic frame and hook checks passed. Native GameInput host control skipped under Wine.
- Observed a second instance of the scheduled TLS sequence at +0x52d5bcf with another widening destination. Accepted the register field while retaining exact instruction/addressing validation. Rerun command: /tmp/vicebridge-checkpoint-run.sh 53eb4b7 52d5bcf.
- Latest trace /tmp/vicebridge-checkpoint.juDLHa/startup.jsonl. After successful task queue creation, two threads hit the same allocator failure breakpoint. Observed request arguments were size 0x4500, alignment 8 and size 0x80, alignment 16. This is an allocator-path failure, not evidence of host memory exhaustion. There were no D3D12 calls or game frame.

## Allocator observability and GameInput timing

- Added requested and returned bases, raw allocation flags and protection to the first XMemVirtualAlloc trace for each distinct attribute. LastError is preserved. The XMem fixture verifies a successful mapping and error preservation, and the synthetic Vulkan frame regression passed.
- An instrumented isolated run recorded non-null results for all five startup XMem allocation profiles, including both 24 GiB reservations. The subsequent allocator failure therefore cannot be attributed to any of those five calls returning null. The guest dispatch path uses its own allocation size-class selector, so debugging is now focused on guest allocator state and its thread initialization.
- Another run encountered an integer divide fault on a GameInput callback path. GameInput now initializes the performance-counter frequency before guest entry and guards the divisor. The GameInput regression passed, and the latest isolated run did not reproduce the divide fault. The guest allocator breakpoint remains at +0x22106e0, with a worker breakpoint at +0x3bf4. Latest trace /tmp/vicebridge-checkpoint.SaOkLE/startup.jsonl. No graphical initialization or game frame was observed.

## Default attach mode and DirectStorage entry

- Compared both startup modes with the same TLS site list. In default mode the title reached +0x162b78f with an invalid context pointer. Its producer uses four slot-zero reads (643cd5, 643cea, 643d03, 643d30). Added narrow support for an intervening MOV immediate and RIP-relative LEA; executable regressions and the ASM suite passed.
- With those sites redirected the title reached DStorageGetFactory. PDB identified another host CRT local-static guard failure in DirectStorage IID policy. Moved DirectStorage process-lifetime state to host startup, preserving original lifetimes and lock scopes. DirectStorage fixture passed.
- Latest default-mode launch reached DirectStorage factory creation, OpenFile and the SDL gamepad backend. It then stopped at +0x6138c5 trying to dereference an invalid object pointer. No D3D12 events or title frame. Latest trace and launch transcript: /tmp/vicebridge-checkpoint.dCLXcO/. Reproduction: /tmp/vicebridge-loader-thread-run.sh 53eb4b7 52d5bcf 643cd5 643cea 643d03 643d30.

## Fault after initial DirectStorage open

- Traced the +0x6138c5 object to a guest TLS accessor and redirected its three verified slot-zero reads (1fb2314, 1fb2329, 1fb2341). Follow-up runs exposed and redirected two more consumers (22374bb, 4da6364). Allocator breakpoints still appear intermittently.
- An execute fault at address zero was erroneously labeled as a host RVA under Wine and had no unwind frames. Fixed the null address formatting and added a separate raw stack-top field for execute violations. A mapped synthetic null indirect call verifies both the address and stack word. Build and startup_trace passed.
- Latest offline run /tmp/vicebridge-checkpoint.KSZncE reached DirectStorage factory and OpenFile again. The new trace identified the null indirect call at +0x787ecb (return address +0x787ecd). It loads the target from the object's first vtable slot. The producer and lifetime of that object still need tracing. No graphics or frame. Reproduce with /tmp/vicebridge-loader-thread-run.sh 53eb4b7 52d5bcf 643cd5 643cea 643d03 643d30 1fb2314 1fb2329 1fb2341 22374bb 4da6364.

## Object lifetime and physical-page trace

- The object used at +0x787ecb is allocated by the guest as a 0x60-byte object, and its constructor stores a guest vtable at +0x788544. The destructor dispatch later sees 0x100002620 instead of that vtable. The exact write changing the object remains unverified.
- The constructor also reads slot zero at +0x788568. Isolated runs with that verified site redirected exposed an intermittent host crash in XMem's physical-page trace deduplication. Moved that trace state to host initialization, sharing the existing attribute trace mutex. XMem regression covers physical attribute deduplication and passed.
- Latest run /tmp/vicebridge-checkpoint.xbISfQ again reached DirectStorage but stopped at the guest null call, with an allocator failure breakpoint on a worker. No D3D12 calls or frame. Reproduction adds 788568 to the prior default-mode site list.
- Added failure tracing for XMemAllocatePhysicalPages, including original count and Win32 error. Logging runs after releasing the physical page lock and restores LastError. The invalid-request regression and XMem suite passed.
- The latest isolated run (/tmp/vicebridge-checkpoint.CkB2X8/startup.jsonl) stopped earlier at a worker's guest allocator breakpoint (0x4500 bytes, alignment 8). No physical-page request or failure was traced before this stop. A preceding run with the same TLS sites (/tmp/vicebridge-checkpoint.xbISfQ/startup.jsonl) again reached DirectStorage and the null destructor dispatch, so the observed stopping point varies with startup scheduling. No graphics or game frame captured.

## Premature object release

- GDB hardware watchpoints twice caught the first word of the second constructed object changing from the expected vtable (0x146209118) to 0x100002620. The writer at +0x540abec is in the guest allocator's deallocation path, not in a DirectStorage read. The saved return addresses tie it to the destructor at +0x787f40, called by temporary cleanup at +0x788bd3 before the outer destructor call. Evidence retained in build/diagnostics/run.8rg47D/launch.log and main.log.
- The temporary wrapper constructor reads hard-coded slot zero at +0x788cdb to decide whether to initialize its ownership flags. Redirecting this verified read let a run go beyond the null destructor call, through DirectStorage open and GameInput, to new TLS-backed faults at +0xf22e6 and +0x555b07e. Their slot-zero loads are +0xf22c1 and +0x555b066. This is progress through the affected path; the allocator breakpoint still occurs on other schedules.
- The local ignored helper build/diagnostics/offline-run.sh f22c1 555b066 uses a shared Wine prefix and read-only package. Latest run with both sites stopped on the intermittent allocator breakpoint. No D3D12 or game frame yet. Watchpoint repro uses build/diagnostics/offline-watch.sh and build/diagnostics/object-watch.gdb.

## Same-thread startup through display mode

- Retested same-thread process attach against the previous default-mode checkpoint. Redirected verified TLS reads and their paired writes along the new call paths. Kept all rewrites explicit and in memory.
- Extended the trampoline to validate REX-extended TLS vector registers, an intervening base-relative LEA and a register copy after a RIP-relative operand. Executable tests passed for those encodings, including rejecting a mismatched vector base.
- The title reached DirectStorage OpenFile, SDL GameInput and XDisplayTryEnableHdrMode, with an SDR result. No D3D12 events and no game frame. Latest trace build/diagnostics/run.NP3OAX/startup.jsonl shows the next read fault at +0x80e89a and the worker breakpoint at +0x3bf4.
- Reproduction from the checkout: bash build/diagnostics/offline-entry.sh f22c1 555b066 293ee5e 264860 9dfa1a 103787 20acb34 20acb49 20acb61 405dbf9 1d9a1c0 1d70093 1d70112 115912.

## Scheduled TLS reads after display initialization

- Extended the explicit TLS trampoline matcher to cover verified stack-save, register-widen and register-copy encodings. Executable synthetic probes passed (code_hooks, 2.63 s).
- Explicitly redirecting +0x80e86c and +0x80e680 advanced the main thread to +0x1d70580. The read at +0x1d70523 selected a per-thread object via hard-coded slot zero. Redirecting it advanced execution into the call at +0x1d706af. The next read fault is +0x115bee with a noncanonical pointer operand. The worker breakpoint at +0x3bf4 still occurs.
- Latest run: build/diagnostics/run.rdryKO/startup.jsonl and main.log. DirectStorage OpenFile, GameInput and SDR selection were reached. No D3D12 calls or frame observed.
- Reproduce: bash build/diagnostics/offline-entry.sh f22c1 555b066 293ee5e 264860 9dfa1a 103787 20acb34 20acb49 20acb61 405dbf9 1d9a1c0 1d70093 1d70112 115912 80e86c 80e680 1d70523

## TLS reads in object selection and worker cleanup

- The invalid pointer at +0x115bee came from the nonzero field at offset 0x1258 of the slot-zero TLS block. Added trampoline support for the intervening register ADD used at +0x115bd0 and +0x40794e1. The executable probe validates the add result, flags, relocated slot selection and rejection of a truncated sequence. Windows targets built and code_hooks passed (2.60 s).
- Add 115bd0 40794e1 to the preceding reproduction command. Two isolated runs passed the previous read fault. run.6XEaJs hit the intermittent allocator breakpoint and run.HtrKF9 hit a worker cleanup read fault at +0x1b9c887. Inspection traced the worker pointer to another explicit slot-zero read at +0x5f028d5.
- Add 5f028d5 to reproduce the latest run, build/diagnostics/run.EoS38P/startup.jsonl. The main thread next faulted at +0x104075 dispatching through allocator pointer 0x4c4c. Its caller +0x1972ee2 received the pointer through +0x1972c90 on the +0x407795a path. The worker breakpoint at +0x3bf4 remains. No D3D12 calls or frame observed.

## First observed D3D12 device request

- The allocator pointer at +0x104075 came from the 0x13a8 cache selected through hard-coded TLS slot zero at +0x1972de9. Redirecting that read advanced the main thread to a write fault in the TLS-backed path stack (+0x128b997, run.HGwLYU). Redirected the depth check and push at +0x128b960/+0x128b980, then the separate object-selection read at +0x115e14 observed in run.MAG75W.
- With all four sites, run.OijSgp reached D3D12XboxCreateDevice. The ordinary Wine backend returned E_NOTIMPL and the guest broke at +0x19f4f58. Evidence is in build/diagnostics/run.OijSgp/launch.log and startup.jsonl.
- Append 1972de9 128b960 128b980 115e14 to the previous command. Local build/diagnostics/offline-vulkan.sh uses the same arguments and explicitly selects LEONIDA_D3D12=vulkan. Four Vulkan runs (JSYZD0, Ezce5C, yD2BoE, saucSK) stopped before device creation at the intermittent allocator breakpoint +0x22106e0. The allocator dispatch reaches size-class allocation at +0x4dac530, which can return null if its slab acquisition at +0x4dac720 fails. Capture the actual return from the upstream allocator call at +0x4dac7c4 to determine whether this is the failing branch. No device, submission, Present or frame verified.

## GTA VI creates a Vulkan device

- Hardware probes reached the guest allocator error without first observing a null slab callback at +0x4dac7c6. A separate trace reached the larger-allocation path at +0x69e12 on a 0x24a0 request. Single-CPU affinity allowed runs to reach graphics initialization despite the intermittent allocation failure.
- Replaced Vulkan loader dynamic local initialization with an atomic cache that retries failed discovery. Added System32 lookup and Win32 load-error reporting. run.UXe8wv successfully created a Vulkan device on the GTX 1650, then faulted on its first virtual call because the proxy's first word was null.
- The proxy table also used dynamically initialized local storage. Making it constant-initialized let run.FeoW6E create the device and reach device slot 26. The guest call places a result buffer in RCX and the object in RDX. Generic PC forwarding reads RCX as the object and faults at runtime +0x6088c. The guest call sites +0x247870d and +0x2478731 vary R9 across heap types, consistent with a structure-return ABI difference on GetCustomHeapProperties. Verify the signature and keep the desktop ABI working when introducing the typed route.
- Reproduce with the previous site list under taskset -c 0 bash build/diagnostics/offline-vulkan.sh. Latest logs are build/diagnostics/run.FeoW6E/{launch.log,startup.jsonl} and main.log. Build and code_hooks passed. The Vulkan integration test exercised GPU copies but failed its one-packed-tile assertion at d3d12_vulkan_device_test.cpp:6564. No game frame or screenshot.

## Guest queues and fence

- Added separate, constant-initialized device tables for the desktop and Xbox structure-return ABIs. Slot 26 explicitly translates the hidden Xbox result pointer and calls GetCustomHeapProperties with the native COM ABI. The mock exercises both argument orders and compares the returned properties.
- Initialized console slot routing with Win32 InitOnceExecuteOnce. The previous CRT local initialization could leave the route unset on a guest thread, forwarding slot 48 to OpenExistingHeapFromAddress. The guest now creates a standard queue, a fence and its first Xbox compute queue. All six xs_proxy_mock CTests passed, including slot overrides.
- Moved Vulkan log deduplication state to host startup. It now reports the next rejection cleanly instead of crashing on the error path. Run CMhcvG reaches the second Xbox compute queue and returns E_INVALIDARG. The 16-byte console descriptor contains words 2, 0, 0x04030201, 0x08070605. The current desktop interpretation rejects the last two words; determine the console field semantics before translating them. Reproduce with the unchanged single-CPU Vulkan command. No submission, Present or frame observed.

## Xbox queue descriptor projection

- CreateCommandQueueX now projects the observed Type/Priority prefix onto a real single-node host queue. The two console scheduling words remain uninterpreted. Ordinary CreateCommandQueue keeps validating the desktop Flags/NodeMask. The mock verifies descriptor translation and clears the output when host creation fails.
- The single-CPU Vulkan launch (run.UpYWxf) passed all four CreateCommandQueueX calls and reached command allocator and list creation. Slot 73 rejected console list IID c3827890-4024-474c-98cf-645b51dc4874. After successful creation the guest immediately calls list slots 132 and 131 before Close, so an IID alias to a desktop object would be unsafe. The pre-revert proxy in 57c175391^ documents mappings and ownership, but does not implement the two high slots and its full revert cites a broken synthetic frame path.
- Build and xs_proxy_mock passed. Logs and test result are in main.log and build/diagnostics/run.UpYWxf/. No command submission, Present or game frame.

## Command-list ABI and GPU regression

- The reserved-resource assertion was invoking Xbox SetFrameIntervalX at slot 78 through a desktop Device10 interface. The backend test now queries the actual desktop device for that method and asserts the returned resource exists before measuring its tiling. It still checks the packed-tile count through the proxy's GetResourceTiling. Both d3d12_vulkan_device and run_tiny_frame passed.
- Compared the first two console command-list calls with the pre-revert proxy and independent first-boot measurements: neither establishes the semantics of slots 131 and 132. The following Close call is confirmed at slot 9. Bare PC IID substitution would leave the two high slots outside the PC table. The existing failure at CreateCommandListX is preserved until those calls have compatible behavior.
- Independent measurements: https://github.com/LooperSalty/AnyXboxX/blob/main/docs/research/first-boot-1021.md. GPU test output is in main.log. Game trace remains run.UpYWxf, with no command submission or game frame.

## Xbox command-list creation and submission validation

- CreateCommandListX now returns a separate Xbox ABI object for the observed full IID, backed by a real D3D12 command list. COM lifetime, known recording calls, Close and Reset use the backing list. Desktop IIDs retain their original path. Queue submission unwraps closed Xbox lists and rejects open lists or recordings containing unsupported slots.
- The mock validates creation, Close/Reset, forwarding the underlying list to the queue, and rejection of a recording containing an unsupported operation. xs_proxy_mock and run_tiny_frame passed.
- The title reached successful command-list creation in run.8PyyBW. It then attempted slots 132 and 131. The proxy rejected the recording and returned E_NOTIMPL from the guest-facing Close after closing the backing list. The title broke at +0x2478bd4. See build/diagnostics/run.8PyyBW/launch.log and main.log. The semantics of the two operations still need implementation. No real game submission, Present or screenshot.

## Xbox list bindings

- Added descriptor-heap and compute/graphics root-signature and root-constant calls using the measured Xbox list slots. The heap route unwraps heap proxies and rejects invalid array shapes. The mock asserts the actual desktop method indices and forwarded arguments. xs_proxy_mock and run_tiny_frame passed.
- Compared the slot 131 argument with the public Mesa GFX10.3 register map. Interpreting the two observed values as dword register offsets yields unrelated registers (TA_CS_BC_BASE_ADDR and DB_OCCLUSION_COUNT0_LOW). This does not establish the Xbox operation or answer slot 132. Both still reject the recording at Close, as seen in run.8PyyBW. No game frame.

## GPU submission through the Xbox command-list proxy

- Added the measured slot 146 enhanced barrier route. It passes one barrier group to the backing D3D12 list and rejects the recording if the group or interface is unavailable. The mock checks the group count and pointer on the real desktop slot.
- The mapped tiny_frame fixture now also creates a console-IID list through slot 73, records an enhanced barrier and CopyBufferRegion, submits through the queue proxy, waits on the fence, and compares the readback bytes. xs_proxy_mock and run_tiny_frame passed. Test results are recorded in main.log.
- Retail startup still needs semantics for the two initialization calls at slots 131 and 132 before its list can be submitted. The GPU verification used only the synthetic fixture. No game Present, frame or screenshot.

## Xbox texture copy on Vulkan

- Connected measured Xbox command-list slot 16 to the typed CopyTextureRegion API. The PE fixture clears previous readback contents, copies the rendered texture through the Xbox list, submits, then rechecks all 32 pixels alongside the separate buffer copy. The Windows build and run_tiny_frame passed.
- Checked the local test package for a separate d3d12 module that could provide behavior for slots 131/132. No such module is present. Their game-facing semantics and the first game frame remain unresolved.

## Retail list initialization trace

- The rebuilt runtime records arguments for unsupported Xbox list slots. Run nzMKiP confirmed slot 132 takes (0x700000, 0, 0, 0xffffffffff00), then slot 131 takes (0xc3c0, 0x80020000, 0, 0xffffffffff00). This matches the independent static measurements. The exact operations are still unidentified. No game submission or Present.
- Repro uses the unchanged one-CPU Vulkan command. Evidence is in build/diagnostics/run.nzMKiP/launch.log and main.log. xs_proxy_mock passed with the trace change.

## Xbox input-assembler routes

- Connected list slots 47 and 48 to native index and vertex buffer binding. The proxy rejects vertex views outside the D3D12 slot range before forwarding. The mock checks both native slot mappings and the out-of-range rejection. GPU buffer addresses pass through unchanged, so buffers using console unified addresses still depend on address translation.
- Built gta6rt and xs_proxy_mock_test. xs_proxy_mock and run_tiny_frame passed. The retail command-list initialization calls at 131/132 remain unresolved.

## Xbox root GPU addresses

- Connected slots 41–46 to the typed compute/graphics root CBV, SRV and UAV setters. The mock checks all six mappings and retention of the full 64-bit GPU address. Addresses must already be valid in the host GPU address space; unified-address translation is still pending.
- Build, xs_proxy_mock and run_tiny_frame passed. The retail initialization remains stopped by slots 131/132.

## Xbox resource barriers and resolves

- Routed list slots 26 and 19 to typed ResourceBarrier and ResolveSubresource. A nonempty barrier request with a null array invalidates the recording. The mock verifies both methods' argument order and validates 32-bit fields at their declared ABI width.
- Build, run_tiny_frame and the corrected xs_proxy_mock passed. This route passes desktop-compatible resource states through unchanged. Retail slots 131/132 still stop initialization before submission.

## Xbox mapped upload address translation

- Implemented slot 148 with real persistently mapped D3D12 upload buffers. The proxy returns an aligned CPU address and translates addresses from that allocation for root CBV/SRV/UAV and index/vertex views on the same list. The list retains allocations across Reset and releases them with the list. Other unified-address regions and cross-list lookups are outside this implementation.
- The mapped PE fixture verifies overflow rejection and a writable 256-byte-aligned allocation. xs_proxy_mock and run_tiny_frame passed. No shader consumption through a translated upload address has been verified yet, and retail 131/132 still stop the game before submission.

## Upload translation boundaries

- Added range validation for vertex and index views of slot-148 allocations. A view beginning inside a mapped allocation cannot span past its requested size, even if the backing GPU buffer includes alignment padding. Native addresses outside known upload allocations continue through unchanged.
- Mock coverage now verifies CPU to GPU offset translation for root and vertex views, preserves the caller's view, checks the out-of-range failure, and confirms unmap and release at list destruction. Windows build, xs_proxy_mock and run_tiny_frame passed. Retail slots 131/132 still stop startup before submission.

## Retain translated uploads during deferred replay

- The Xbox list now registers referenced upload buffers with the backing Vulkan recording. Deferred queue replay captures these references alongside pipeline and root-signature references, so releasing or resetting the list cannot drop the allocation before the deferred batch is submitted. The Vulkan backend already defers buffer and memory destruction for submitted work.
- The mock verifies the retain request and the mapped PE fixture exercises a real translated vertex-buffer binding. Windows build, xs_proxy_mock and run_tiny_frame passed. A deferred batch with an Xbox upload has not been exercised end to end. Retail 131/132 still stop the title before submission.

## Deferred Xbox batch regression

- The mapped PE fixture now holds its Xbox list behind an unsignalled fence, queues a completion signal, releases the public list, verifies the completion fence has not advanced, then opens the gate. run_tiny_frame passed, including GPU texture and buffer readback after deferred replay.
- The deferred recording includes a translated upload vertex view. It still does not draw from that view, so a shader or draw consuming the translated memory is unverified. Retail 131/132 remain unresolved and this verification produced no game frame.

## GPU draws from translated Xbox uploads

- Added Xbox list routes for viewports, scissors, render targets and color clears. The geometry regression uses the Xbox list and allocates its vertex, index and root CBV data through slot 148. It defers submission behind a fence, releases the list before signaling the gate, then compares the rendered pixels.
- d3d12_geometry and run_tiny_frame passed on Vulkan. This verifies translated upload consumption in the synthetic graphics pipeline. Retail slots 131/132 still prevent the first game submission and no game screenshot exists.

## High Xbox device slots

- Traced startup after the first list Close. It allocates and closes additional lists, calling the same slots 132/131 with the second argument set previously observed. The local boot-chain measurements identify device slot 133 as a later placed-resource call.
- Extended the device vtable through slot 159. The known high slot 133 now returns a named E_NOTIMPL instead of falling outside the table. The mock verified this path and run_tiny_frame passed. No placement compatibility was inferred; the physical resource will need coherent CPU/GPU backing.

## Retail rejection confirmed on current build

- The guest-facing Close trace now reports its own HRESULT and preserves the closed state if a repeated Close fails. Run kOJY1q reproduces slot 132 then 131 followed by Xbox Close returning E_NOTIMPL, distinct from the backing list closing successfully. No submission or Present follows.
- Available independent boot measurements still identify these two operations only by index and arguments. xs_proxy_mock and run_tiny_frame passed. Trace is build/diagnostics/run.kOJY1q/launch.log.

## Host-backed Vulkan buffer feasibility

- Built and ran the local Windows probe build/diagnostics/host-import.cpp under the same Wine prefix as the title. On the GTX 1650, VK_EXT_external_memory_host is exposed with 4096-byte alignment. A committed VirtualAlloc pointer passed vkGetMemoryHostPointerPropertiesEXT (compatible buffer memory mask 0x18), vkAllocateMemory with the host pointer, and vkBindBufferMemory.
- This establishes a route for real backing for device slot 133, subject to actual Xbox page-run ownership and lifetime. No shared read/write or imported Xbox page range has been verified. Probe output is in build/diagnostics/host-import.log and main.log. Retail slots 131/132 still precede slot 133 and no frame was obtained.

## Coherent host-memory import on the target GPU

- Extended build/diagnostics/host-import.cpp to select a Vulkan host-visible coherent memory type and submit a GPU copy between disjoint ranges of the imported buffer. CPU wrote 0x4d79c621, GPU copied it, and CPU read the expected bytes on GTX 1650 under Wine.
- Both VirtualAlloc and a pagefile-backed section passed. In the section case the CPU used a second view of the same section, which is the alias model needed to keep a stable Vulkan import while the guest maps its own view. Actual XMem multi-page runs, import lifetimes, and slot 133 dispatch are pending. Log is build/diagnostics/host-import.log and main.log. Retail still stops before submission at 131/132.

## Stable aliases of XMem physical runs

- Added a writable independent view for aligned ranges backed by consecutive physical pages in one XMem section. Lookup uses the actual section and offset kept at map time, so recycled page numbers cannot select a new backing. Gaps, reordered pages and unmapped ranges are refused.
- The xmem test verified that writes through the alias reach the guest and that the alias survives guest unmap. Windows build and xmem passed. Vulkan import of that alias and slot 133 dispatch remain to be connected. Retail still stops earlier at slots 131/132.

## GPU transfer across actual XMem pages

- Linked build/diagnostics/host-import.cpp with the project's XMem shim and registry objects. The probe reserves a guest window, allocates and maps two XMem pages, imports an independent section alias with VK_EXT_external_memory_host, writes the first page from the guest view, and has the GPU copy four bytes into the second page. Guest readback matched 0x4d79c621 on the GTX 1650 under Wine.
- Evidence in build/diagnostics/host-import.log and main.log. The probe destroys the Vulkan buffer and frees its device memory before unmapping the alias. The production resource path still needs that cleanup ordering tied to the queue retirement ticket and the slot-133 dispatch. Retail continues to stop earlier at 131/132.

## XMem alias ownership across address and page reuse

- Fresh placeholder reservations discard stale backing metadata over their address range. Ordered mapped-page tracking makes the cleanup proportional to the affected range. A guest mapping that remains live still holds its original section independently of the page-number registry.
- The xmem regression now frees and reallocates page numbers while the old mapping is live, then verifies that an alias still sees the old contents and the newly allocated page reads as zero. Build and xmem passed. Production Vulkan import and the earlier retail command-list blocker remain open.

## Retirement ordering for imported Vulkan buffers

- Added a deferred deletion item that owns the Vulkan buffer, imported device memory and host-storage release callback together. On the retirement ticket it destroys the buffer, frees device memory, then releases the host mapping. It does none of those operations while the ticket is outstanding.
- vk_queue passed with a regression for timing, destruction order and single execution, and run_tiny_frame passed before the final grouped-retirement refinement. Slot 133 still needs to enqueue this item from the resource path. Retail 131/132 still stop startup before submission.

## Slot 133 imports XMem buffers in the Vulkan runtime

- Routed Xbox CreatePlacedResourceX to a Vulkan-backed buffer for contiguous XMem sections. The device enables external host memory when advertised, selects coherent host memory, and binds the imported alias without zeroing the guest's bytes. Retirement destroys the buffer, frees device memory, then unmaps the alias. The implemented path accepts row-major buffers with supported flags and the observed unspecified/current initialization states. Texture placement, scattered ranges, and unverified Xbox flags remain open.
- The mapped PE fixture passed an actual slot-133 call on GTX 1650 and verified GPU copy from the first XMem page to the second through the guest mapping. xs_proxy_mock, xmem, vk_queue and run_tiny_frame passed. Evidence: build/diagnostics/slot133-test.log.
- The subsequent offline title run nsBuEw still stops before submission, at Xbox list slots 132/131 and Close. No game frame or screenshot.

- Slot 133 GPU regression now uses the observed Xbox initial-state sentinel (0xffffffff), and verifies that a misaligned guest XMem address returns E_INVALIDARG with a cleared output. run_tiny_frame passed and reported the actual XMem GPU copy in build/diagnostics/slot133-state-test.log.

- The Vulkan importer now verifies the alias against the driver's alignment before querying it. A rejected host pointer reports the actual VkResult and returns E_INVALIDARG, so the fixture's optional-capability skip cannot hide a broken address import. run_tiny_frame again reported an actual slot-133 GPU copy in build/diagnostics/slot133-alignment-test.log.

- Slot 133 replay regression unmaps the guest XMem range before submission and remaps it after the fence. The expected bytes persisted through the independent GPU import. run_tiny_frame passed (build/diagnostics/slot133-remap-test.log).

## Retail passes initial Xbox command-list sizing calls

- An independent XCL investigation (https://github.com/xcl-dev/xcl/issues/505) identifies slots 131/132 as init-only native PM4 buffer sizing. The host list records into a dynamically sized IR. Accept the two observed configuration forms for each slot without emitting native PM4, and still reject unrecognized calls. xs_proxy_mock passed for accepted and rejected arguments.
- run.vkTbn1 stopped on the intermittent allocator breakpoint. run.mLCt8a progressed beyond the sizing calls and recorded 517 successful native command-list creations with closes during graphics setup. The next title fault is +0x17f6c65: invocation of a Release-shaped call through a null vtable. The pointer was read from the output slot of device slot 9 called at +0x17f6bcf, after the intervening list creation loop. Inspect that pointer and its first word immediately after the slot-9 call and before Release. No submission or Present was observed.

## Allocator proxy and DXGI startup

- Hardware watchpoints in run.Hi1DyZ show slot 9 returned S_OK with a non-null allocator object whose first word was already zero. Neither the local pointer nor the vtable changed before +0x17f6c65. Moved the guarded child tables and the overrun handler to InitOnceExecuteOnce, avoiding the guest thread's CRT local-static initialization path. All six xs_proxy_mock cases passed.
- run.7aY6qa passed the allocator Release and reached GetAdapter. The DXGI reservation map used the same problematic initialization pattern; moved it and DXGI diagnostic state to process startup. dxgi_device and run_tiny_frame passed.
- run.qAsPZL passed GetAdapter, SetFrameIntervalX and ScheduleFrameEventX, then completed the 10 MiB XMem replacement reservation. The title then returned to the graphics-init breakpoint +0x19f4f58; the last observed device call was slot 15. No GPU submission, Present or game frame.

## Retail reaches slot 133 with a fragmented XMem range

- GDB localized the graphics-init failure to the first physical page map. Wine traced MapViewOfFile3 receiving PAGE_WRITECOMBINE on an ordinary section and returning error 87. Removed the view-level cache modifier while preserving CPU access rights. xmem and run_tiny_frame passed.
- The title reached slot 133. GDB captured the row-major buffer descriptor with flags 0x20004 and state 0xffffffff. The Vulkan route keeps the desktop UAV flag and uses original XMem backing in place of the observed Xbox allocation flag. The synthetic GPU test now uses that descriptor flag word and passed.
- run.awq3BN identifies the next blocker precisely: the 174 MiB guest range is mapped, but the backing offsets cease to be consecutive at 27 MiB. The contiguous section-alias path refuses it. A stable alias spanning multiple backing runs or sparse external-memory binding is required. No submission, Present or frame observed.

## Fragmented XMem GPU import probe

- build/diagnostics/fragment-import.cpp links the current XMem shim and imports a synthetic guest range made of two independently mapped pages in reversed backing order. The GTX 1650 under Wine accepted the pointer and copied 0x4d79c621 between pages. This rules out a driver requirement for consecutive section offsets on this configuration.
- Production must import an independent composite alias, not the mutable guest view used in the probe. Construct it by keeping section references under the XMem lock, reserving an independent placeholder and mapping runs in guest order; retain those views until the existing Vulkan retirement callback. No retail frame yet. Probe output: build/diagnostics/fragment-import.log.

## Retail imports its fragmented XMem resource

- Slot 133 now imports an independent composite XMem alias in guest page order. It maps contiguous backing runs into a new placeholder, holds the alias through Vulkan retirement, and releases the views after the buffer and device memory. xmem and run_tiny_frame passed with reordered backing; the GPU check also unmaps the guest view before submission.
- run.UWXvde passed the fragmented slot-133 allocation and created the first descriptor heap, then read through a null heap vtable. Converted that vtable to constant initialization. run.eqjytA reached the heap's GetCPUDescriptorHandleForHeapStart and hit the guest assertion at +0x1cf336c. The next gate is matching the expected guest descriptor base to the host heap. No game submission, Present or screenshot.

## Retail passes the placed descriptor heap check

- GDB confirmed the first extended heap carries an XMem base at +0x10 and a 38 MiB byte extent at +0x18. Recognized matching aligned placement descriptors now produce a heap over imported XMem. Its CPU handles use the guest mapping and shader-visible GPU handles use the imported buffer VA. run_tiny_frame verified handles and a descriptor write into XMem.
- run.L82YE9 hit the intermittent allocator breakpoint. run.wqonlc passed the original heap assertion, created four more heaps and reached CreateShaderResourceView. The next fault is illegal instruction at +0x4ecc84a, confirmed by disassembly as CLWB [r9]. No game submission, Present or screenshot.

## Cache-writeback adaptation reaches the next descriptor ABI

- The explicit `--clwb-rva` option verifies the executable instruction bytes before entry. It preserves native CLWB where supported and emits a same-length CLFLUSHOPT or CLFLUSH sequence on older CPUs. The executable probe checks the instruction and its continuation. `code_hooks` and `run_tiny_frame` passed.
- `run.U27bwh` passed the original CLWB fault, created SRVs, UAVs and a CBV, then reached Xbox device slot 70. The generic PC forwarding called a method that treats its sixth parameter as a writable output pointer. The Xbox caller passes only three arguments after the device, and the host attempted to write to an address in guest code. The remaining work at this gate is translating the three-argument descriptor contract. No game submission, Present or screenshot.
