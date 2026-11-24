# Audio/DSP static progress and remaining implementation gap

This is a static investigation of the local retail executable and a small XApu connection-lifetime fix.
No game code, TLS callback or DllMain was run. No audio samples or instruction bytes are included here.

## Boot failure is not optional

The audio-initialization routine calls ApuCreateHeap, a HAL initialization helper, and XApuConnect in that order.
Each failed result leads to the same breakpoint and a false return. The HAL helper requires shape-context
allocation, real ApuAlloc memory, AcpHalCreate, and non-negative returns from object slots 0 and 2.
Consequently the current AcpHalCreate E_NOTIMPL is a real boot blocker, not a graceful no-audio mode.
This independently agrees with the newer upstream documentation proposal
[PR #7864](https://github.com/ItsNotRexed/Leonida/pull/7864). That PR is documentation, not an audio backend.

## Measured XApu command/result facts

There are four direct XApuEnqueueCommand call sites, with command-type words **1, 2, 3 and 5**.
The earlier upstream descriptions listing only 1, 2 and 3 omit the fourth site's type 5.
All four pass a connection handle, a command pointer and a zero third argument.
Their first two command dwords are a command type and a per-stream index.

The type-3 command is built in a 64-byte stack region. Measured stores relative to the command pointer:

| Offset | Value written by the caller |
|---|---|
| +0 | 3 (command type) |
| +4 | stream index |
| +8, +16 | zero qwords |
| +24 | zero dword |
| +28 | float conversion of a caller-owned unsigned dword; its semantic meaning remains unknown |
| +32 | another caller-owned dword; semantic meaning unknown |
| +36 | 0x3c0 (960) |
| +40 | input length minus 2 |
| +44 | input pointer into a shared-buffer slot; the pointer is not 8-byte-aligned within the structure |
| +52 | a caller-owned byte multiplied by 0xf00 (3840) |
| +56 | output pointer into a shared-buffer slot |

Before submission, the caller copies input from its source pointer plus 2 with length minus 2.
The shared-buffer input stride is 0x280; output addresses use base + 0x14000 + slot * 0x1e00.
960 float32 samples per channel would account for 3840 bytes; this is an inference, not proof of the codec,
sample format, channel bounds, resampling rules or the meanings of the fields at +28 and +32.
In particular, no Opus/XMA decoder has been selected merely from those constants.

XApuGetResultCount's result is used as a bounded dequeue-loop count. XApuDequeueResult writes to a local
0x50-byte buffer. A negative result skips processing. For successful results the caller reads:

- type at +0;
- stream index at +4;
- an additional dword at +0x34 for one type-3/state-4 transition.

Result types 1, 2, 3 and 5 enter different state-machine paths. Type 2 invokes a cleanup helper. The remaining
paths inspect per-stream states before changing them. Producing the command type and stream index alone is
therefore insufficient to claim a correct result implementation, especially for decoded audio payloads.

## Measured XDsp ABI facts

XDspConnectWithMaximumStreamLimit is called with six arguments. Its fourth is 0x80, fifth is 1, and sixth is an
out-handle pointer. A zero result is success. The imported XDspActivate wrapper swaps its third and fourth
register arguments and checks for a zero result. XDspSubmitCommand's wrapper swaps its first two arguments;
which argument is a DSP connection remains unproven. These observations do not establish command payloads or
completion behavior. The five XDsp exports remain unimplemented rather than pretending to execute commands.

## Connection-lifetime fix

Measured XApu call sites treat the stored connection handle as opaque: they pass it back to the XApu imports.
Direct references to the containing global structure are also used to address adjacent stream-state fields;
those accesses do not dereference the connection value itself. Aliases and indirect uses are not exhaustively
proven absent by this limited static scan.

The old shim used a heap-object pointer as the opaque handle and deleted that object on disconnect. Allocator
address reuse could make a retired handle accidentally refer to a later connection. The shim now stores
connections under process-lifetime uintptr_t IDs. Zero is invalid, successful publication consumes an ID,
failed registry allocation does not consume one, and wrap to zero exhausts the ID space instead of reusing IDs.
Disconnect removes the ID only after VirtualFree succeeds. This changes no audio-processing claim.

Synthetic regression coverage reconnects 32 times, checks every retired ID against each new live connection,
verifies that the new connection still works and that its shared memory stays intact. Merely reconnecting did
not reproduce reuse on this Wine CRT, so the test has a narrow allocator fixture: during connect calls only,
a pointer-sized object can use one aligned slot, never simultaneously for two live objects. Ordinary allocation
outside that gate, and all larger/over-aligned objects, remain outside slot reuse. The fixture supports throwing
and nothrow new and corresponding normal/sized/nothrow deletes. Shared-buffer VirtualAlloc is unaffected.

Isolated synthetic builds with clang-cl 21 and the existing xwin SDK, executed under Wine 10.0:

- the old b4eb9e734 XApu implementation: 342 checks, 93 failures; 32 forced connection-slot allocations;
- the fixed implementation: 1272 checks, zero failures; no pointer-sized connection-object allocation.

The failed old checks concern reused retired handles reaching the new connection. The new implementation
passes the same test fixture. This tests lifetime and validation behavior; it does not exercise or implement
audio decoding. The compile and runs used one low-priority job restricted to CPUs 2 and 3.

## Still required

A usable software audio backend needs the command semantics, stream configuration fields, decoder/format,
result payload rules, the HAL object's command contract and actual output scheduling. Neither upstream
#6885/#6981's command-dropping success nor #7009's broad stubs establishes those behaviors. No new backend
or successful DSP completion is claimed by this change.
