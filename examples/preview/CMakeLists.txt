cmake_minimum_required(VERSION 3.20)

project(example_charts VERSION 0.1.0 LANGUAGES NONE)

include("${CMAKE_CURRENT_SOURCE_DIR}/HuxerUIProject.cmake")
if (HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif ()

file(GLOB_RECURSE APP_SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)
if (NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    if (WIN32)
        set(APP_PLATFORM_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/main.cpp")
    elseif (APPLE AND NOT IOS)
        set(APP_PLATFORM_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/main.cpp")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(APP_PLATFORM_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/main.cpp")
    endif ()
    list(APPEND APP_SOURCE_FILES ${APP_PLATFORM_MAIN_SOURCE})
endif ()

huxerui_add_app(example_charts
        SOURCES
            ${APP_SOURCE_FILES}
        RESOURCES
            resources
        RESOURCE_NAMESPACE
            app
        ${HUXERUI_APP_RESOURCE_OUTPUT_ARGUMENTS}
        BUNDLE_NAME
            "Charts Preview"
        BUNDLE_IDENTIFIER
            "org.huxerui.lib.charts.preview"
)

huxerui_configure_project_app(example_charts)

if (TARGET HuxerUI::Charts)
    huxerui_use_library(example_charts
            TARGET HuxerUI::Charts
    )
else ()
    huxerui_use_library(example_charts
            TARGET HuxerUI::Charts
            PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    )
endif ()

huxerui_add_app(rounded_donut_demo
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/../rounded_donut_demo.cpp"
            ${APP_PLATFORM_MAIN_SOURCE}
        RESOURCES
            resources
        RESOURCE_NAMESPACE
            app
        ${HUXERUI_APP_RESOURCE_OUTPUT_ARGUMENTS}
        BUNDLE_NAME
            "Rounded Donut Demo"
        BUNDLE_IDENTIFIER
            "org.huxerui.lib.charts.rounded-donut-demo"
)

huxerui_configure_project_app(rounded_donut_demo)

if (TARGET HuxerUI::Charts)
    huxerui_use_library(rounded_donut_demo
            TARGET HuxerUI::Charts
    )
else ()
    huxerui_use_library(rounded_donut_demo
            TARGET HuxerUI::Charts
            PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    )
endif ()
