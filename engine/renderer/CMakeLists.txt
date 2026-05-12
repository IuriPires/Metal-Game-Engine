# engine/renderer

add_subdirectory(rhi)
add_subdirectory(frame_graph)

if(APPLE)
    add_subdirectory(metal)
endif()

# Future:
# add_subdirectory(passes)         # M6+
