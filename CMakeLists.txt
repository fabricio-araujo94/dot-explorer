cmake_minimum_required(VERSION 3.16)
project(dot-explorer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Definitions
add_compile_definitions(_DEFAULT_SOURCE _XOPEN_SOURCE=700)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Threads
find_package(Threads REQUIRED)

# Curses / Ncurses
# Try to find curses with wide-character support
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses QUIET)

# Fallback: pkg-config if Curses not found or on MinGW/Linux
if(NOT Curses_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(NCURSES QUIET ncursesw ncurses)
    endif()
endif()

# Core library with common sources (excluding main.c)
set(CORE_SOURCES
    src/fs.c
    src/input.c
    src/process.c
    src/state.c
    src/task.c
    src/ui.c
    src/utils.c
    src/utils/theme.c
)

add_library(dot-explorer-core STATIC ${CORE_SOURCES})
target_include_directories(dot-explorer-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(TARGET Threads::Threads)
    target_link_libraries(dot-explorer-core PUBLIC Threads::Threads)
endif()

if(Curses_FOUND)
    target_include_directories(dot-explorer-core PUBLIC ${CURSES_INCLUDE_DIRS})
    target_link_libraries(dot-explorer-core PUBLIC ${CURSES_LIBRARIES})
elseif(NCURSES_FOUND)
    target_include_directories(dot-explorer-core PUBLIC ${NCURSES_INCLUDE_DIRS})
    target_link_libraries(dot-explorer-core PUBLIC ${NCURSES_LIBRARIES})
else()
    target_link_libraries(dot-explorer-core PUBLIC ncursesw)
endif()

# Main executable
add_executable(dot-explorer src/main.c)
target_link_libraries(dot-explorer PRIVATE dot-explorer-core)

# Tests
option(BUILD_TESTING "Build the testing tree" ON)

if(BUILD_TESTING)
    enable_testing()

    macro(add_dot_test test_name)
        add_executable(${test_name} tests/${test_name}.c ${ARGN})
        target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${test_name} PRIVATE Threads::Threads)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endmacro()

    add_dot_test(test_fs src/fs.c src/state.c src/task.c src/utils.c)
    add_dot_test(test_state src/state.c src/fs.c src/utils.c)
    add_dot_test(test_utils src/utils.c src/utils/theme.c)
    add_dot_test(test_task src/task.c src/fs.c src/utils.c)
    add_dot_test(test_input src/input.c src/process.c src/fs.c src/state.c src/utils.c)
    add_dot_test(test_process src/process.c)

    # test_ui requires curses/ncurses
    add_executable(test_ui
        tests/test_ui.c
        src/ui.c
        src/input.c
        src/process.c
        src/fs.c
        src/state.c
        src/task.c
        src/utils.c
        src/utils/theme.c
    )
    target_include_directories(test_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(TARGET Threads::Threads)
        target_link_libraries(test_ui PRIVATE Threads::Threads)
    endif()
    if(Curses_FOUND)
        target_include_directories(test_ui PRIVATE ${CURSES_INCLUDE_DIRS})
        target_link_libraries(test_ui PRIVATE ${CURSES_LIBRARIES})
    elseif(NCURSES_FOUND)
        target_include_directories(test_ui PRIVATE ${NCURSES_INCLUDE_DIRS})
        target_link_libraries(test_ui PRIVATE ${NCURSES_LIBRARIES})
    else()
        target_link_libraries(test_ui PRIVATE ncursesw)
    endif()
    add_test(NAME test_ui COMMAND test_ui)
endif()
