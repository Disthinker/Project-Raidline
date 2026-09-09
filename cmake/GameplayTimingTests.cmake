# CTest includes this after gtest_discover_tests has populated the real cases.
# Wall-clock budgets measure the simulation, not contention with other tests.
# Ordinary GameplayWorld cases remain parallel; no assertion/threshold changes.
foreach(raidline_timing_test IN LISTS GameplayWorldTest_TESTS)
    if(raidline_timing_test MATCHES "^GameplayWorldPerformanceTest[.]")
        set_tests_properties("${raidline_timing_test}" PROPERTIES RUN_SERIAL TRUE)
    endif()
endforeach()
unset(raidline_timing_test)
