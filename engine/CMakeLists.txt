# engine/
#
# Each subsystem is its own static library and gets added here as
# milestones land. M0 ships only `mge_core` as a sanity target.

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(memory)
add_subdirectory(scene)
add_subdirectory(assets)
add_subdirectory(profile)

if(APPLE)
    add_subdirectory(renderer)
    add_subdirectory(platform)
endif()

# Future (added per milestone):
# add_subdirectory(jobs)        # Phase 1.5+
# add_subdirectory(ecs)         # Phase 2+
