# ViceBridge initial validation

24 November 2026, Linux host; low-priority builds limited to CPUs 2 and 3, one compile job.

- Native CMake/Ninja configure and build succeeded for xg_xs_linear_test, shader_library_test and pipeline_cache_file_test.
- Native CTest: 3/3 passed.
- Windows cross configure/build succeeded with clang-cl 21, lld-link 21 and the existing xwin SDK 10.0.26100.
  Built gta6rt, gta6rt_tls_host and the selected synthetic tests. Legacy compiler warnings remain; build had no errors.
- Windows CTest under Wine: 7/7 passed (xg_xs_linear, shader_library, xmem, dstorage, acphal, xapu, xg_xs).
- Python: test_fspx_unpack (35, 1 skip), test_fspx_shaders_stage (22), test_build_runtime_fallback (8, 1 skip),
  test_import_call_sites (8), test_pe_inspect (60, 1 skip). All passed: 130 successful tests and 3 Windows-only skips.
- make_game.py --help succeeded. Diff whitespace and tracked-file game-data checks passed.

The XG test opts into its experimental model only inside the synthetic test process; the runtime defaults remain disabled.
Tests use synthetic inputs. No game entry point, game TLS callback or game DllMain was executed in this project setup.
No full GPU corpus check, rendering validation, complete inherited test suite, asset export or menu/startup test is claimed.
No game content, generated game resources, SDK files or binaries were added to Git.
