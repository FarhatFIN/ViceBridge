# Local XG host-linear subset

This is a bounded adaptation of the XG code in [upstream integration PR #8134](https://github.com/ItsNotRexed/Leonida/pull/8134),
at `c77a4fa275e70d24de70f8adac9b5272a27f1c25`. It implements an owned texture-computer object, measured layout
fields and CPU copies **only for an explicitly requested linear layout (`0x100`)**. It does not implement native
Xbox swizzle modes, automatic mode selection, sparse/reserved resource tiling, texture upload to Vulkan images,
or all of the XG interface. Returning an owned object for a supported request is distinct from the static dummy
object in the earlier #6891 shim; unsupported creation clears the output and returns an error.

**The experimental model is disabled by default.** Both exports return `E_NOTIMPL` without fabricating an object
or modifying a mode field unless `LEONIDA_XG_LINEAR=1` explicitly opts in. The synthetic Windows test enables it
only inside its own process. Registering these exports does not make the default runtime's texture pipeline complete.

## Evidence and its limits

The local executable was inspected statically; no executable, TLS callback or DllMain was invoked. No game
instruction bytes or disassembly are included here.

| Observation | Evidence | Status |
|---|---|---|
| Descriptor has the public `D3D12_RESOURCE_DESC` offsets: dimension `+0`, alignment `+8`, width `+0x10`, height `+0x18`, depth/array `+0x1c`, mips `+0x1e`, format `+0x20`, sample count `+0x24`, quality `+0x28`, layout `+0x2c`, flags `+0x30` | Descriptor-building helper `0x4ecc940`, reached through the thunk at `0x4ecc7f0` | Directly observed stores; names inferred from the matching public structure and callers |
| Source mode zero is represented as `0x100` in descriptor layout | Helper at `0x4eccb6a`; other known modes are ORed with `0x100` at `0x4eccb32` | Encoding directly observed; its interpretation as linear follows #8134 |
| Computer vtable slot 2 receives a layout output; plane starts at `+0x18`, plane stride is `0x6c8` | Calls at `0x979c9f`, `0x5121a84`; plane copy at `0x5121aa9` | Directly observed |
| Mip record stride is `0x70`; consumers read size at `+0`, slice size at `+0x10`, row pitch at `+0x1c` | Layout reads starting at `0x979ca5` | Offsets directly observed; field names follow uses and #8134 |
| Slot 8 receives base, plane, subresource, source, row pitch and slice pitch | Call at `0x16f71fb` | Argument placement directly observed; semantic names inferred from the preparation loop and #8134 |
| Slot 1 is used after the object is no longer needed | Calls at `0x16f7233`, `0x5121a8f` | Direct call observed; Release interpretation inferred |
| Slot 10 is used for reserved-resource information | Call at `0x344283` and #8134 | Argument shape observed; complete layout semantics unverified, so it stays unsupported |

The complete XG layout structure is not a public, locally verified contract. The `0x18` resource header,
`0x38` plane header, and record fields beyond the three measured fields follow #8134's Xbox One XG-layout
interpretation and remain assumptions. The implementation writes the four plane slots but does not append the
unmeasured dimension word present in that draft. Only one plane is nonzero. Its support claim is restricted to
the observed consumer subset, not binary equivalence with Microsoft's XG library.

The two observed layout outputs have sufficient stack space for this model's write: the caller at `0x979c40`
reserves `0x1b98` bytes and passes `RSP+0x58`; the caller at `0x5121980` reserves `0x1ba0` and passes `RSP+0x60`.
A `0x1b38`-byte write ends eight bytes before each allocation ends. This establishes capacity at these two
sites, not the meaning of every returned field or a generic output-size contract.

The suggestion call at `0x4eccca2` receives the address of the descriptor's `+0x2c` layout field. After the call
the helper only forwards the HRESULT; it does not OR the returned value with `0x100`. The separate known-mode
path performs that OR itself. Consequently this explicit-only implementation preserves `0x100`, not raw mode zero.

Microsoft's [public DirectXTex Xbox example](https://github.com/microsoft/Xbox-ATG-Samples/blob/main/Kits/DirectXTex/DirectXTexXboxTile.cpp)
uses texture-address computers, resource layout queries and subresource copies, but calls `XGCreateTexture2DComputer`
with an XG texture description. It does not establish this title's generic `XGCreateTextureComputer` ABI.
Do not substitute the sample's older structure for the measured descriptor.

## Implemented behavior

- Validated 1D/2D arrays and 3D textures, one sample, explicit layout `0x100`, zero resource flags. Width, height,
  depth and mip counts are bounded. Nonzero resource flags remain unsupported until their linear-layout contract
  is established, including flags some real title calls use.
- A fixed allowlist of single-plane byte and BC formats. Unknown, video, packed two-pixel, one-bit, depth/stencil
  and multiplane formats return an error; no unknown format defaults to four bytes per pixel.
- Host-linear footprints with 256-byte rows, 512-byte slice placement and 64-KiB resource allocation alignment,
  inherited from the draft's PC placed-footprint convention. This is not a native Xbox tiling calculation.
- Mip-major storage of all array slices, and depth slices for 3D. Subresource numbering uses mip plus array times
  mip count. Layout fields required by the observed consumers and per-subresource CPU copies agree with each other.
- Slots 0/1 manage reference lifetime, slot 2 writes the bounded layout, slot 8 copies into a supported subresource.
  Other slots return `E_NOTIMPL` and do not fabricate outputs. Slot 10 does not invent packed-tile metadata.
- Short row/slice pitches, insufficient spans, invalid plane/subresource and overlap reject before writing.
  The actual ABI has no buffer-length arguments: the caller must provide mapped buffers of the computed sizes.
  Windows SEH catches an access violation during copying, but a mapping fault can occur after a partial write;
  copying is not transactional for inaccessible mappings.
- `XGSuggestSwizzleMode` only validates and preserves an explicit linear request. An unknown or tiled request
  returns `E_NOTIMPL` without changing the caller's field. It does not silently force the title to linear memory.

## Synthetic validation and integration

`runtime/test/xg_xs_linear_test.cpp` is portable and needs only `runtime/shims` as an include path. It checks
golden row/slice/mip offsets, BC edge dimensions, arrays and volumes, invalid/unsupported descriptors, output
preservation, insufficient spans, partial overlap, and slice-pitch validation.

`runtime/test/xg_xs_test.cpp` needs Windows, `shims/xg_xs.cpp`, `shims/registry.cpp`, the `shims` include path,
`WIN32_LEAN_AND_MEAN`, `NOMINMAX`, and `/OPT:NOREF`. It resolves exports through the registry and calls slots 0, 1,
2, 8 and 10 in the measured shapes. Compile `xg_xs.cpp` with `/EHa` under clang-cl so the SEH copy guard is active.
It needs no Vulkan driver and no game data.

The portable test was compiled with GCC C++20, `-O1 -Wall -Wextra -Werror`: 34 checks passed. The Windows ABI test
must be built and run by the integrator before this shim is installed. No retail game path or rendered output
has been tested. The entire title's texture pipeline remains incomplete even if both names become bound.
