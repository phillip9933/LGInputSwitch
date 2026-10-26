cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
project(LGInputSwitch LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(lginput_core core/input_switcher.cpp)
target_include_directories(lginput_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

if(WIN32)
    enable_language(RC)
    add_executable(LGInputSwitch WIN32
        app/main.cpp app/app_tray.cpp app/app_config.cpp app/app_toggle.cpp
        app/hotkeys.cpp app/settings_ui.cpp app/welcome_ui.cpp
        amdddc/adl.cpp amdddc/amdddc_core.cpp platform/windows/adl_transport.cpp
        resource/app.rc)
    target_include_directories(LGInputSwitch PRIVATE app amdddc external adl-sdk/include resource)
    target_compile_definitions(LGInputSwitch PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(LGInputSwitch PRIVATE lginput_core user32 shell32 advapi32 gdi32)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(lginputswitch cli/main_linux.cpp platform/linux/ddcutil_backend.cpp)
    target_link_libraries(lginputswitch PRIVATE lginput_core)
    install(TARGETS lginputswitch RUNTIME DESTINATION bin)
endif()

if(BUILD_TESTING)
    add_executable(regression tests/regression.cpp app/app_toggle.cpp app/hotkeys.cpp
        platform/windows/adl_transport.cpp)
    target_link_libraries(regression PRIVATE lginput_core)
    target_include_directories(regression PRIVATE amdddc)
    if(NOT WIN32)
        target_include_directories(regression PRIVATE tests/win32)
    endif()
    add_test(NAME regression COMMAND regression)
    add_executable(core_regression tests/core_regression.cpp)
    target_link_libraries(core_regression PRIVATE lginput_core)
    add_test(NAME core COMMAND core_regression)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME linux-cli COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_cli.py $<TARGET_FILE:lginputswitch>)
    endif()
    if(WIN32)
        add_executable(startup_test tests/startup_windows.cpp)
        target_link_libraries(startup_test PRIVATE advapi32)
        set_target_properties(startup_test PROPERTIES OUTPUT_NAME "Startup Test")
        add_test(NAME startup COMMAND startup_test)
    endif()
endif()
