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
