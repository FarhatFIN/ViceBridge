# Contributing to ViceBridge

Use local branches and focused changes. Preserve inherited authorship and licenses. Read [AGENTS.md](AGENTS.md).
Original Leonida contributor guidance is archived in CONTRIBUTING.leonida.md; its old coordination links are historical.
ViceBridge work does not require permission or claims in an unavailable upstream issue.

Never commit game files, extracted resources, generated shaders, keys, binaries, or logs quoting game content.
Do not decrypt protected inputs, bypass access controls, or call Xbox Live/Rockstar online services.
Use synthetic fixtures, distinguish measured ABI facts from hypotheses and report unsupported operations honestly.
Do not manufacture a success response for functionality that does not exist.

Run targeted tests, compile changed native code, check `git diff --check` and run
`python3 -I scripts/check_no_game_data.py` before committing. Keep generated results in ignored directories.
State which checks ran and which did not. Game execution requires an explicit separate decision; routine tests use synthetic data.
