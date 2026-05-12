# Sanitizer toggles. Apply via target_link_libraries(<tgt> PRIVATE mge_sanitizers).

add_library(mge_sanitizers INTERFACE)

set(_mge_san_flags "")

if(MGE_ENABLE_ASAN)
    list(APPEND _mge_san_flags -fsanitize=address -fno-omit-frame-pointer)
endif()
if(MGE_ENABLE_UBSAN)
    list(APPEND _mge_san_flags -fsanitize=undefined)
endif()
if(MGE_ENABLE_TSAN)
    list(APPEND _mge_san_flags -fsanitize=thread)
endif()

if(_mge_san_flags)
    target_compile_options(mge_sanitizers INTERFACE ${_mge_san_flags})
    target_link_options(mge_sanitizers INTERFACE ${_mge_san_flags})
endif()
