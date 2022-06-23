cmake_minimum_required(VERSION 3.13)
project(bookmark_manager)

file(GLOB SRC "src/*.cpp")

add_library(bookmark_manager SHARED ${SRC})
target_link_libraries(bookmark_manager PRIVATE sdrpp_core)
set_target_properties(bookmark_manager PROPERTIES PREFIX "")

target_include_directories(bookmark_manager PRIVATE "src/" "../../decoder_modules/radio/src")

if (MSVC)
    target_compile_options(bookmark_manager PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(bookmark_manager PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(bookmark_manager PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS bookmark_manager DESTINATION lib/sdrpp/plugins)