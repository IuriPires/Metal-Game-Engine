# engine/renderer

add_subdirectory(rhi)

if(APPLE)
    add_subdirectory(metal)
endif()

# Future:
# add_subdirectory(frame_graph)    # M5
# add_subdirectory(passes)         # M6+
