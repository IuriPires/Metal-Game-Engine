# engine/renderer

if(APPLE)
    add_subdirectory(metal)
endif()

# Future:
# add_subdirectory(rhi)            # M3
# add_subdirectory(frame_graph)    # M5
# add_subdirectory(passes)         # M6+
