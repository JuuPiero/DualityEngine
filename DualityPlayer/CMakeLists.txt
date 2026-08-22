add_executable(DualityPlayer Source/Main.cpp)
target_link_libraries(DualityPlayer PRIVATE DualityEngine)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    # Produces DualityPlayer.3dsx next to the .elf, using devkitPro's own
    # CMake helper (ships with the installed devkitARM-cmake package) --
    # default icon/name/description for now, customized in a later phase.
    #
    # ROMFS packages romfs/Scene.json (a real scene saved from the Editor,
    # copied in manually for now -- an automatic Assets/-to-romfs cook step
    # is a later phase) so it's readable on-device as romfs:/Scene.json.
    # Note: this plain-directory ROMFS form isn't dependency-tracked by
    # CMake, so a `cmake --build` won't notice romfs/ contents changing on
    # their own -- reconfigure/rebuild (build-3ds.bat always does a clean
    # build) to pick up an updated Scene.json.
    ctr_create_3dsx(DualityPlayer ROMFS romfs)
endif()
