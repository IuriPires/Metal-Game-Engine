# Engine warning baseline. Apply via target_link_libraries(<tgt> PRIVATE mge_warnings).

add_library(mge_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(mge_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
    if(MGE_WERROR)
        target_compile_options(mge_warnings INTERFACE -Werror)
    endif()
endif()
