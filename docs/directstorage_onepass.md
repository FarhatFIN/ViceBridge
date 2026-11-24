# DirectStorage: zlib and physical-page destination integration

This local change adapts existing upstream work rather than independently claiming its implementation:

- [PR #7180](https://github.com/ItsNotRexed/Leonida/pull/7180), commit `00551ba0099699f35893ecd58a9ec34a2b54e95c`: zlib requests and inflater error reasons.
- [PR #7652](https://github.com/ItsNotRexed/Leonida/pull/7652), commit `d528947d468fdb3388f1605ad8c99db0725bd6e4`: physical-page destinations using the xmem page backing.

It preserves this branch's test-only before-copy checkpoint, which makes the pending-statistics test independent
of Wine PAGE_GUARD exception delivery. Production builds contain no checkpoint callback.

## Implemented paths

A file or memory request with ZlibDecompress reads its compressed source into a bounded staging buffer, then uses
the existing RFC 1950/1951 inflater from shader_library.h. The header and Adler-32 are checked. The result must
be exactly DestinationSize bytes before it is written, so a corrupt stream does not overwrite the destination.
A memory source may overlap its destination. Trailing bytes after the zlib trailer are ignored as in upstream.
Requests still follow the console size/alignment rules chosen by #7180: zlib destinations exceed their source size,
zlib file offsets are 16-byte aligned, and IntermediateSize remains zero without BCPack.

A DestinationIsPhysicalPages request copies its page-number array out of title memory first. The data is written
through the existing xmem sections, so already mapped aliases see it and an unmapped page retains it for later mapping.
Pages may be out of backing order and come from several allocations. Every page number is checked before any write.
The helper acquires every section view before copying, so a mapping failure does not leave an earlier run partially
written. Allocation failures in staging buffers and helper metadata reservation are caught, and unsuccessful view cleanup is logged. Shared section references keep
backing storage alive if the caller concurrently frees its page numbers.

Unpaged address destinations retain the 32 MiB buffer limit. Physical-page destinations use 64 KiB pages and may
exceed that limit; the pre-existing 1 GiB combined-request limit still bounds staging. This is not a fixed 32 MiB
staging-memory guarantee for physical-page requests.

## Evidence and limits

The added synthetic cases exercise stored/fixed/dynamic Huffman streams, header/data/checksum errors, truncation,
size mismatches, overlapping memory, invalid buffers, decompression statistics, page-boundary crossings, multiple
allocations, mapped/unmapped pages, a copy above 32 MiB, invalid/unreadable page arrays and freed page numbers.
The existing xmem suite remains a regression check. Build/test results are recorded by the parent integration task;
this document does not claim a retail boot or data-corpus test.

BCPack requests, swizzle requests and SourceIsPhysicalPages still return E_NOTIMPL. The Python BCPack decoder
already present in tools/ is distinct from a runtime DirectStorage backend. As documented in textures.md, parts of
its BC6H/BC7 handling are provisional; this pass does not turn those into a complete native decoder.

The names/values of the three zlib-specific HRESULTs were inherited from #7180's header-generated bindings source.
Their mapping to header/data/Adler errors is an inference, not a measurement of console hardware. A bad physical-page
number maps to E_DSTORAGE_ACCESS_VIOLATION locally; that exact console failure code is unverified. Whether console
DirectStorage requires a page to be mapped is also unverified. Existing factory/vtable and structure-layout caveats
in directstorage.md still apply.

No game entry point, game DLL initializer or game TLS callback was run. No game bytes, keys, shaders, executables,
archives or calibrated history data are included in this source change.

A process-wide out-of-memory guarantee is not made: diagnostic formatting and log_once bookkeeping still allocate,
as in the pre-existing worker/error paths. An allocation failure there can escape the staging-buffer catches.
The existing inflater itself stays within the destination capacity reserved before decoding. CPU alias visibility
is covered by the synthetic page-window tests; no Vulkan GPU-alias test or retail streaming workload was run here.

Validation for this integration: the parent build completed with the existing xmem missing-field-initializer warning;
shader_library, xmem and dstorage CTests all passed under Wine. No additional game code execution was performed.
