cmake_minimum_required(VERSION 3.20)
project(FlameUp VERSION 1.0 DESCRIPTION "Windows Backup Tool")

# Set C++ standard and enable required features
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For filesystem library
find_package(Threads REQUIRED)

# Create the executable
add_executable(FlameUp main.cpp)

# Link libraries
target_link_libraries(FlameUp PRIVATE
        Threads::Threads
)

# Only link stdc++fs for older GCC versions
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(FlameUp PRIVATE stdc++fs)
endif()

# Set compiler warnings
if(MSVC)
    target_compile_options(FlameUp PRIVATE /W4 /WX)
    # Disable the specific warning about localtime
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(FlameUp PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

# Only define install if it's a release build
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Install to a relative directory instead of system location
    install(TARGETS FlameUp
            RUNTIME DESTINATION bin
    )
endif()