cmake_minimum_required(VERSION 3.22)
project(SYNChestra VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
set(JUCE_DIR "${juce_SOURCE_DIR}")
include(cmake/PatchJuce.cmake)   # see the file: stops a crash with some plugins through yabridge

juce_add_gui_app(SYNChestra
    PRODUCT_NAME "SYNChestra"
    COMPANY_NAME "SYNChestra")

target_sources(SYNChestra PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/PluginScanner.cpp
    Source/Core/SampleMap.cpp
    Source/Core/NoteRenderer.cpp
    Source/Core/Library.cpp
    Source/Core/Looper.cpp
    Source/Core/Project.cpp
    Source/Core/LibraryPlayer.cpp)

target_compile_definitions(SYNChestra PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SYNChestra,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SYNChestra,JUCE_VERSION>")

target_link_libraries(SYNChestra PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Link-time optimisation makes a smaller, faster app, but GCC 16 crashes doing it on JUCE's
# HarfBuzz, so it is off there by default. -DSYNCHESTRA_LTO=OFF turns it off anywhere else.
set(lto_default ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    set(lto_default OFF)
endif()
option(SYNCHESTRA_LTO "Build the app with link-time optimisation" ${lto_default})
if(SYNCHESTRA_LTO)
    target_link_libraries(SYNChestra PRIVATE juce::juce_recommended_lto_flags)
endif()

# Checks of the recorder and the .sfz/.dspreset writers against a stand-in synth:
#   SYNChestra_tests OUTPUT_FOLDER
juce_add_console_app(SYNChestra_tests PRODUCT_NAME "SYNChestra_tests")
target_sources(SYNChestra_tests PRIVATE
    tests/cpp/LibraryTest.cpp
    Source/Core/SampleMap.cpp
    Source/Core/NoteRenderer.cpp
    Source/Core/Library.cpp
    Source/Core/Looper.cpp
    Source/Core/Project.cpp
    Source/Core/LibraryPlayer.cpp)
target_compile_definitions(SYNChestra_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SYNChestra_tests PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Linux install and packages: `cmake --install build --component SYNChestra` (JUCE adds its own
# install rules, which the packages leave out), and `cpack` in the build folder for .deb and .rpm.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS SYNChestra RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT SYNChestra)
    install(FILES packaging/linux/SYNChestra.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT SYNChestra)

    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};SYNChestra;SYNChestra;/")
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGE_NAME synchestra)
    set(CPACK_PACKAGE_VENDOR "SYNChestra")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Record instrument plugins into Decent Sampler and SFZ libraries")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Sharkuel/SYNChestra")
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sharkuel <https://github.com/Sharkuel/SYNChestra>")
    set(CPACK_DEBIAN_PACKAGE_SECTION sound)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # JUCE opens the X11 libraries at run time, so no tool finds them in the binary.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0-only")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_REQUIRES "libX11, libXext, libXrandr, libXinerama, libXcursor")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications)
    include(CPack)
endif()
