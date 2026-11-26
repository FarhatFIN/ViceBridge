# Blockers

- Authorized title launch: null read in library_loader.cpp host_module_name during GetProcAddress. Cause remains unproven.
- AcpHal object and audio completion contract incomplete; AcpHalCreate returns E_NOTIMPL.
- Native texture data association and automatic upload are incomplete.
- Vulkan validation layer unavailable in current Wine environment; DXGI comparison has discrepancies.

Next: construct a synthetic loader/TLS regression before changing the host lookup path.

Static caller analysis confirms the failing title frame calls GetModuleHandleW followed by GetProcAddress.
A fresh-thread first-lookup regression passes under Wine, so that alone does not reproduce the crash. Next reproduce
the call from a mapped synthetic executable after its CRT/TLS initialization, and inspect host TLS before lookup.
