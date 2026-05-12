# tests/
#
# Subdirectories are added as milestones produce code worth testing.

add_subdirectory(unit)

if(APPLE)
    add_subdirectory(integration)
endif()

# Future:
# add_subdirectory(golden)       # M3+
# add_subdirectory(perf)         # M8+
