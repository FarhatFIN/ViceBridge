# AcpHal object: static caller contract

24 November 2026. Read-only analysis of the locally supplied PE image; no entry point, TLS callback,
DLL initializer or other game code executed. This records caller facts, not a working audio backend.

Image SHA-256: `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
Image length: 120,181,760 bytes. Addresses below are RVAs, not virtual addresses.
Tools: existing `import_call_sites.py` / `pe_inspect.py` and local Capstone; no instruction bytes included.

## Direct observations

`AcpHalCreate` has one direct caller at `0x43aed98`, through thunk `0x5f40170`.
Its first argument addresses a global at `0x9ae98b8`. The caller tests the returned low 32 bits for a negative
value and takes a false-return path on failure. This supports an out-object/HRESULT interpretation.
It does not establish all creation parameters or the complete object ABI.

| Object slot | Call RVA | Arguments established by the caller | Result use |
|---|---|---|---|
| 0 | `0x43aedba` | object, `0x1000`, `0x1000` | negative 32-bit result takes initialization failure path |
| 1 | `0x43ae84f` | object; no additional argument setup at this site | ignored |
| 2 | `0x43aee2f` | object, 2, `0xf0f0f0f0`, `0xffffffff`, pointer to a dword containing `0xc0`, 0 | negative 32-bit result makes initialization return false |
| 2 | `0x43a7416` | object, 5, `0xf6f6f6f6`, `0xffffffff`, pointer to caller-supplied stream/index dword, 0 | ignored; a per-entry state is then set to 2 |
| 2 | `0x92619` | object, 7, input index + 1, `0xffffffff`, pointer to input index dword, 0 | ignored; per-entry state/flag updated |
| 2 | `0x78521` | object, `0x12`, `0xf1f1f1f1`, `0xffffffff`, data pointer, 0 | ignored; at least two payload dwords are written before submission |
| 5 | `0x43ae88b` | object; no additional argument setup at this site | ignored; global object pointer is cleared afterwards |

The global-reference scan found eight MOV/LEA RIP-relative references, in five unwind-function ranges.
This is a scoped scan: other addressing forms, indirect aliases, callbacks and computed uses can be missed.
It does not prove that unobserved slots are unused.

After slot 0 succeeds, initialization creates an event using the imported `CreateEventA` with
null security attributes, manual reset enabled, initial state clear and null name. The handle is stored
in a separate global. A signed shared state is polled while negative; only afterwards is slot 2 called.
Shutdown calls slot 1, signals the event using imported `SetEvent`, waits while a shared state is positive,
then calls slot 5. These observations do not identify the writer of the shared state.

A subsequent linear Capstone pass over every executable section decoded 26,674,487 instructions in 27.1 seconds.
For shared-state RVA `0x9ae98c8`, it found four direct RIP-relative reads (the two polling loops) and one direct
write of zero at initialization (`0x43aec56`). No other direct RIP-relative state access was found by this pass.
The image contains no literal 64-bit virtual address of that state. This narrows the next investigation to
indirect/alias access, initialization structures or external component behavior; it does not prove the state
never changes. Linear disassembly is not control-flow recovery and may misidentify embedded data or miss code.

## Inferences and implementation gate

The repeated slot-2 shape suggests a command dispatcher, and slots 1/5 participate in shutdown.
Their names, ownership rules, payload sizes beyond observed writes, command meaning, completion protocol
and exact return types are not established. The AcpHal command identifiers above are distinct from
the XApu command-type words in [audio_dsp_progress.md](audio_dsp_progress.md).

A six-slot dummy object that accepts calls would conceal missing work. A real implementation still needs:

- the shared-state writer and worker/event handshake, including failure and shutdown behavior;
- ownership and lifetime of submitted payloads, cancellation and command completion;
- the shape-context processing contract and its relationship to XApu decoding;
- an established decoder/sample-format contract before selecting an audio codec or claiming playback.

`AcpHalCreate` therefore remains `E_NOTIMPL`. Existing heap/context helpers are not evidence that hardware
initialization or the game's audio bootstrap can complete.
