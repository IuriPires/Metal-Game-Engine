# engine/
#
# Each subsystem is its own static library and gets added here as
# milestones land. M0 ships only `mge_core` as a sanity target.

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(memory)

if(APPLE)
    add_subdirectory(renderer)
    add_subdirectory(platform)
endif()

# Future (added per milestone):
# add_subdirectory(jobs)        # M2/M10
# add_subdirectory(profile)     # M2
# add_subdirectory(assets)      # M4
# add_subdirectory(ecs)         # M4
