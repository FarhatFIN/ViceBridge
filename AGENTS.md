# ViceBridge agent guide

Follow the parent workspace AGENTS.md and this guide. ViceBridge is an independent GPL-3.0 derivative maintained
by FarhatFIN. The inherited guide is archived as AGENTS.leonida.md; its upstream coordination process is historical,
not a requirement to contact or publish to the unavailable original project.

- Investigate code, tests, callers and docs before editing; make a plan, verify changes and review the complete diff.
- Preserve GPL-3.0, third-party notices, Git history and original authorship. See UPSTREAM.md.
- No game content in source control: no archives, executables, extracted assets, generated shaders or quoted game-code logs.
- No keys, decryption, access-control bypasses or online Xbox/Rockstar services.
- Routine tests use synthetic data. Never execute unknown game binaries, callbacks or entry points without separate authorization.
- Keep measured ABI facts, hypotheses and experimental host models distinct. E_NOTIMPL is preferable to fake success.
- Keep generated output outside source or in ignored build/work/reports directories.
- Preserve the existing two-core/low-priority local build policy unless the user changes it.
- Do not post messages, create external releases, or modify other projects as part of a local coding task.

Runtime: runtime/shims and runtime/src. Tools: tools and make_game.py. Unit tests: tests and runtime/test.
Use the existing CMake clang-cl/xwin toolchain for Windows tests on Linux; Wine is a compatibility runtime,
not permission to execute untrusted game code. Portable CMake targets and Python tests need no game data.

Before committing: targeted tests, compiler checks, git diff --check and scripts/check_no_game_data.py.
Document any unverified limitation in STATUS.md. Keep each commit focused and do not claim gameplay from a dry run.
