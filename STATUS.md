# ViceBridge status

Status at project creation, 24 November 2026. Source code and testable development baseline; no verified game menu.

| Area | Implemented scope | Remaining limits |
|---|---|---|
| PE loader | Inherited mapper; synthetic loader/entry/TLS/exception tests passed on previous snapshot | No retail startup test; console boot chain still incomplete |
| Memory | xmem physical pages, AcpHal heap and live-block address mapping | APU address model is inferred; cache policy/hardware not emulated |
| XApu | Connection memory, non-reused opaque handle IDs, explicit unsupported commands | DSP commands/completions and AcpHalCreate not implemented; audio init failure is fatal |
| DirectStorage | Ordinary file/memory reads, zlib, physical-page destinations and ordered completion | BCPack, native swizzle and physical-page sources unsupported; ABI partly inferred |
| XG | Opt-in explicit-linear host model and guarded CPU copy | Disabled by default; native Xbox tiling, automatic swizzle, upload and reserved tiling absent |
| Shader tools | Main corpus previously 26,503/26,503 SPIR-V-valid; FSPX improved from 18 to 29/29, with 29 GPU compute pipelines accepted | Full GPU corpus check incomplete; image/value correctness not proven; wave64 graphics approximate on 32-wide subgroups |

Historical local checks passed storage, xmem, shader-library and XApu suites; the XApu allocation-reuse regression
failed against old pointer handles and passed with non-reused IDs. ViceBridge now builds its Windows loader and passes 3 portable CTests plus 7 Windows CTests under Wine.
Five Python suites ran 133 tests: 130 successful and 3 Windows-only skips. Source-content and whitespace checks passed.
The full inherited test suites and game startup were not run. Original generated game files are not distributed.

Next priorities: prove an audio decoder/command contract, implement native texture layout and uploads, complete
runtime BCPack/swizzle and continue synthetic boot-chain verification. Do not treat bound import counts as readiness.

DirectStorage follow-up: guarded diagnostic request-name reads so an inaccessible name cannot crash the worker
while reporting a rejected request. A synthetic unreadable-name regression reproduced an unhandled Wine page fault
before the fix; the targeted dstorage CTest passed after it, preserving E_INVALIDARG and omitting the unreadable name.
The test target rebuilt successfully. Full-file clang-format checking reports existing formatting violations;
no broad formatting rewrite was applied. No additional streaming feature or game-startup verification is claimed.

Graphics checkpoint (24 November 2026, GTX 1650, Wine, real display): d3d12_present passed with 70 presented
clear frames and swapchain resize/readback; xbox_present and d3d12_draw passed. After fixing artifact-relative
shader-library directory lookup, d3d12_translated_draw and d3d12_m2_textured passed as well. The native
shader_fixture_path test covers packaged-directory precedence, original-path fallback, missing directories and
a file masquerading as a directory. Cross-build configuration needed the installed /usr/bin/llvm-mc-21 explicitly.
Validation-layer runs are blocked by missing VK_LAYER_KHRONOS_validation, not counted as passes.
dxgi_swapchain still fails its host-DXGI comparison (18/45 description results differ under this Wine runtime);
no compatibility relaxation was applied. These are synthetic graphics checks, not game startup or native Xbox
texture-layout validation. Next: synthetic loader/TLS tests, then evidence-backed audio and resource-loading work.

Synthetic boot checkpoint: loader, pe_loader_bounds and startup_trace passed through CTest. The pe_loader and
loader_tls CTest invocations could not find fixtures through Linux absolute arguments; invoking the same built
executables with relative fixture paths passed 79 and 85 checks respectively. LEONIDA_GAME_DIR was unset.
No retail binary was executed. Next investigation: the incomplete AcpHal object/command ABI and the native texture
path; retain explicit unsupported errors until contracts are established. Local logs are in /tmp/vicebridge-*.log.

Native-layout staging checkpoint: d3d12_m2_tiled_upload passes under Wine/GTX 1650. Its synthetic RGBA8 inputs
exercise both standalone GFX10 4K S and 64K D conversion into mapped upload footprints, then real texture copies,
translated shaders, point/linear sampling and pixel readback. Truncated tiled input is rejected without touching
the upload span. The ordinary d3d12_m2_textured regression and native gfx10_tiling/shader_fixture_path tests pass.
This adds an integration test, not automatic runtime resource conversion: XG native mode selection, mip tails,
tile XOR, compressed native uploads and retail resources remain unverified/unsupported.

Compact mip checkpoint: runtime/src/gfx10_compact.h now ports the existing Python compact 2D layout and detiler
for explicit 4K S / 64K D allocations, including smallest-first standalone levels and shared mip-tail origins.
All 16 per-mip reference hashes and 20 odd-size pixel/block chains match the Python implementation on synthetic
address-labelled bytes. Native ASan/UBSan and strict compiler checks passed. Windows gfx10_compact plus ordinary,
standalone tiled and compact-tail M2 GPU tests passed (4/4); the compact GPU check uploads level zero from a tail.
The new helper is not wired to XG exports or automatic game-resource conversion. Arrays, tile XOR, BCPack and
retail verification remain outside this implementation. This is not a claim that the game's texture path works.

Audio static checkpoint: docs/acphal_object_contract.md records image-identified caller evidence for AcpHal
object slots 0, 1, 2 and 5, four distinct slot-2 command identifiers and the event/shared-state handshake.
The worker/completion contract remains unresolved; AcpHalCreate still explicitly fails rather than fabricating
initialization success. No game code was executed for this analysis.

GPU capture checkpoint (25 November 2026): the M2 test optionally writes verified readback RGB bytes as P6 PPM
when VICEBRIDGE_TEST_CAPTURE_PPM names an output file. The compact-tail run captured 64x64 pixels with zero
point/linear comparison errors. RGB SHA-256: 3bd29c5ec98ff362c207603ae934436bd1e9a3fcd36395061261180ed5bd6c51.
The local artifact is build/windows/vicebridge-first-frame.ppm (ignored); a lossless PNG preview is in
/tmp/vicebridge-first-frame.png. An invalid output directory fails explicitly at capture.is_open().
This image is synthetic GPU output, not a game frame. Source-layout-to-CPU-buffer association at Xbox resource
creation remains unproven; normalizing a resource's layout word alone does not establish native data conversion.

Authorized launch attempt (25 November 2026): after the user's explicit request to try launching GTA 6, the current
gta6rt was rebuilt and run against the supplied local package, with its existing Wine prefix, write overlay,
shader library and a 90-second limit. It exited after 3.22 seconds (code 5, no timeout); no game image/menu appeared.
The trace records five mapped modules, their initialization and title TLS callbacks, then a null read in the host
runtime. PDB symbolization identifies host RVA 0xa8242 as the std::map lookup in library_loader.cpp::host_module_name
(line 97), reached through get_proc_address (host RVA 0xa710e). This is a loader/runtime failure before graphics,
not evidence of an AcpHal failure. Its cause is not yet established; next step is a synthetic lifetime/TLS repro.
Local logs and trace: /home/farhat/Games/Leonida/logs/vicebridge-run-20261125-000227.* (not committed).

Loader test follow-up (25 November 2026): CTest now passes relative fixture paths to Windows executables
under Wine. The abort test allows exit 3 in its Wine CRT control runs while requiring the original exact
exit codes from the mapped guest. The targeted run passed 21 tests, with GameInput skipped because the host
DLL was unavailable. The earlier startup crash remains unresolved. Native Windows was not tested.

The broader standard-library run also uncovered stale shader process/readback mocks, a Bink fixture
with no stream header, and a missing Runtime CI trigger for the translator's replace_retry dependency.
The five affected suites passed on rerun. The repository-wide newline and stdout checks still report
violations in existing tools. A complete Windows build and CTest run were started separately.

Multi-module startup follow-up: the synthetic GDK EXE now imports a mapped DLL that resolves a host
export in its CRT constructor. The EXE constructor checks that its entry thread starts with a fresh
copy of the DLL TLS template and that both modules' TLS values survive its own host lookup.
The trace recorded TLS slot 3 for the DLL and slot 2 for the EXE, with 8-byte templates. All four
targeted startup checks passed under Wine. The game's startup failure remains unverified.

The first complete Windows cross-build and Wine run reported 202 passes, 36 skips and 37 failures
across 275 tests. After fixing fixture paths, 19 of those failures passed targeted reruns. The
synthetic mapped guest frame, present and compact texture upload tests also passed on rerun.

Startup diagnostics: a synthetic DLL returning FALSE from DllMain reproduced a generic
`Unknown exception` message under Wine. The loader now copies the original diagnostic before
printing the import report and returns code 1 with the diagnostic and trace event. The startup
command-line test passes its argument, empty-argument and attach-failure cases through the loader.
The native Microsoft UCRT comparison remains enabled on native Windows and was not run here.

Additional targeted reruns passed both GPU XDXR builders, the D3D12 XDXR CPU check and depth
resolve after correcting fixture paths. The Vulkan XDXR CPU check exceeded the first run's
90-second limit and then passed on a snapshot with the original 1000-ray budget.

The second full Wine run reported 222 passes, 36 skips and 17 failures or timeouts in 275 tests.
That run used the earlier CTest configuration for four XDXR entries, which subsequently passed
with relative paths. The API reference state timeout passed on isolated rerun. Residency again
timed out at 120 seconds. The indirect dispatch count failure reproduced separately, and Wine's
shader compiler reported that it could not compile InterlockedAdd for RWByteAddressBuffer.

Title retry (25 November 2026): the current loader mapped five modules and entered the EXE.
The previous host GetProcAddress fault did not recur. The trace recorded a guest TLS callback
read fault at address 0x10, followed by a fatal guest CRT startup read fault at address 0x8.
The EXE was assigned TLS index 2 with a 5248-byte block. No D3D12 trace calls were emitted
and no frame was shown. Wine's debugger stayed attached until the 90-second limit.
Local log and trace: /home/farhat/Games/Leonida/logs/vicebridge-retry-20261125-013451/.

Counted indirect dispatch follow-up: the test now runs its Vulkan readback checks under Wine
without requiring Wine's HLSL compiler to build the native WARP reference. All eight cases
were executed on the GTX 1650. The zero-count case passed, while all seven cases that should
dispatch work read back zero despite successful replay. The runtime cause is unresolved.
The local main.log records the build attempts and full test output.

Counted dispatch and residency follow-up: a direct-dispatch control reproduced the zero readback.
The compute root layout starts at s4, while the synthetic shader read the output address from s0:s1.
With the fixture corrected, the direct and uncounted controls and all eight counted GPU cases passed
on Wine/GTX 1650, including a count overwritten on the GPU. The native WARP comparison was not run.
The residency test also passed under Wine after separating the native host comparison and checking
immediate Vulkan fence completion and monotonicity directly. See the local main.log for full output.

TLS attach follow-up: the synthetic EXE reproduced a dependency read before its slot contained
the guest TLS template. The loader now installs all active thread TLS blocks before it calls any
thread attach callback. Eight targeted checks passed. The offline title retry no longer showed
the previous TLS callback access violation. It next exited with code 5 after 2.57 seconds.
Symbols located the single remaining exception on that run in XMemVirtualAlloc trace bookkeeping.
No D3D12 calls or game frame were observed. Local trace: logs/vicebridge-tls-retry-20261125-192800/
under the Leonida work directory.

The full Wine baseline before these startup changes was 229 passed, 36 skipped and 10 failed out
of 275. The complete output is in the local main.log.

XMem startup follow-up: initialized trace state and resolved placeholder APIs before entering
the mapped CRT. The trace now records failed virtual allocation parameters and preserves the
Win32 error. The original trace-table fault and the 24 GiB reservation rejection did not recur.
The next failure is a guest indirect dispatch through a malformed pointer. The register trace
shows a non-address value at the point of dereference. The source of the bad pointer is still
unknown. The last isolated run exited with code 5 after 2.87 seconds and emitted zero D3D12 calls.
Local trace: logs/vicebridge-startup-20261125-193217/ under the Leonida work directory.

Independent startup implementation: experimental --attach-on-entry delivers DLL process attach
and EXE process TLS callbacks on the thread that enters the EXE. The synthetic startup fixture
checks both module thread IDs and that a rejected DllMain prevents entry. The legacy startup test,
cross-module TLS test, and synthetic Vulkan frame passed under Wine. The experimental mode has
not been validated with a title.

VIctory architecture reference, independent stack handling: --attach-on-entry now reads
SizeOfStackReserve from the mapped PE and applies an 8 MiB minimum. The synthetic 12 MiB
fixture measured 12 MiB on entry and 8 MiB on an ordinary thread. All six targeted startup and
stack tests passed under Wine. The isolated title comparison exited with code 5 after 2.62 seconds
at the same guest instruction as the default mode, before D3D12. The experiment does not resolve
the invalid pointer. Local trace: logs/vicebridge-entry-attach-20261125-194410/.

ASM startup experiment: the failed allocator path uses both PE-indexed TLS and hard-coded slot-zero
reads. ViceBridge now supports opt-in, exact instruction rewrites at user-specified executable RVAs,
with an in-place form and a register-preserving trampoline. Synthetic tests check valid RAX, RCX,
and mixed-register loads, flags and rejected inputs. Eight confirmed sites were rewritten in the
isolated title run. The previous TLS faults and the host missing-export diagnostic crash did not
recur. The title stopped at a later breakpoint after 2.57 seconds (exit 3), with no D3D12 calls.
The log reports an unresolved XErrorReport lookup. Its relation to the breakpoint is unproven.
Local trace: logs/vicebridge-asm-vxxmf474/ under the Leonida work directory.
