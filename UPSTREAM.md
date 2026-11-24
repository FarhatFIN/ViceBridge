# Source provenance and authorship

ViceBridge is independently maintained by FarhatFIN and derives from Leonida (ItsNotRexed/Leonida).
The imported snapshot preserves the original Git history, GNU GPL v3 license and third-party licenses.
The original README and contributor/agent guides remain alongside their replacements with `.leonida.md` names.

Base: original upstream main at `5e54bda2e65dada9bab94a65b2782e919c218d30`.
Imported local integration: `b4eb9e734dd994822e6045f3c34a370ad0a40dcc`, previously submitted as Leonida PR #7763.
The original GitHub repository became unavailable; historical links below describe provenance, not current availability.

Adapted contributions retain credit to their original authors in the inherited history and references:

| Historical Leonida PR | Contribution used |
|---|---|
| [#7216](https://github.com/ItsNotRexed/Leonida/pull/7216) | GameInput ordinal-1 binding, Gustavo Kissel |
| [#4348](https://github.com/ItsNotRexed/Leonida/pull/4348) | Console DirectStorage factory, queues, files and notifications |
| [#6885](https://github.com/ItsNotRexed/Leonida/pull/6885) | AcpHal host memory/context allocation, modified to refuse absent hardware |
| [#6981](https://github.com/ItsNotRexed/Leonida/pull/6981) | XApu connection memory, modified to reject unsupported commands |
| [#7180](https://github.com/ItsNotRexed/Leonida/pull/7180) | DirectStorage zlib requests and inflater error reporting |
| [#7652](https://github.com/ItsNotRexed/Leonida/pull/7652) | DirectStorage physical-page destinations |
| [#7922](https://github.com/ItsNotRexed/Leonida/pull/7922) | Retry incomplete FSPX disassembly with the existing RDNA2 decoder |
| [#8134](https://github.com/ItsNotRexed/Leonida/pull/8134) | Basis for the bounded experimental host-linear XG model |

Some adapted patches were unmerged upstream and carry documented assumptions. Inclusion is not a claim that their
entire subsystem works. Local changes include live-block validation, explicit unsupported audio responses,
never-reused XApu handle IDs, deterministic synthetic regressions, safer physical-page writes and opt-in XG modeling.
See the per-feature notes under docs for exact scope and attribution.
