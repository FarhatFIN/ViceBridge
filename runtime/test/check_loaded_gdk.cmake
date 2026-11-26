# Run the run-time GDK load fixture (tiny_gdk_load) under the loader and check its verdict from outside, as
# check_loaded_async.cmake does: under Wine an exit status alone can look successful even when the guest failed.
get_filename_component(guest_dir "${GUEST}" DIRECTORY)
get_filename_component(guest_name "${GUEST}" NAME)
file(WRITE "${guest_dir}/host_shim_probe.txt" "shim")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef trace_id)
set(trace_file "gdk-${trace_id}.jsonl")
execute_process(
  COMMAND ${RUNNER} "${LOADER}" --run "${guest_name}" --trace "${trace_file}" -- host-shim-probe
  WORKING_DIRECTORY "${guest_dir}"
  TIMEOUT 90
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE output)
file(REMOVE "${guest_dir}/host_shim_probe.txt")
string(REPLACE "\r\n" "\n" output "${output}")
if(NOT code STREQUAL "0" OR NOT output MATCHES "(^|\n)tiny_gdk_load: PASS(\n|$)"
    OR NOT output MATCHES "(^|\n)game exited with code 0(\n|$)")
  message(FATAL_ERROR "Loaded GDK run-time fixture returned ${code}\n${output}")
endif()

file(READ "${guest_dir}/${trace_file}" trace)
file(REMOVE "${guest_dir}/${trace_file}")
string(REGEX MATCHALL [=["kind":"task_queue_created","result":"0x0"]=] queue_results "${trace}")
list(LENGTH queue_results queue_count)
if(NOT queue_count EQUAL 16)
  message(FATAL_ERROR "Expected 16 successful mapped task queue creations, saw ${queue_count}")
endif()
