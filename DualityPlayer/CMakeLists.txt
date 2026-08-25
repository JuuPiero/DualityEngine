add_executable(DualityPlayer Source/Main.cpp)

# GameScripts's Behaviour subclasses only ever get referenced at runtime by
# class-name string (ScriptRegistry::TryCreate), via a self-registering
# static object per class (REGISTER_BEHAVIOUR). Nothing in DualityPlayer's
# own code directly calls a symbol defined in BounceBehaviour.cpp.o /
# ApiShowcaseBehaviour.cpp.o -- so the linker's normal "only extract archive
# members that resolve a pending undefined symbol" behavior drops those
# object files entirely from the final .elf, and their registration
# constructors never run (confirmed empirically: `arm-none-eabi-nm` on a
# built DualityPlayer.elf showed zero BounceBehaviour/ScriptRegistrar
# symbols, despite them being present in libGameScripts.a itself). Every
# BehaviourComponent on-device then silently fails to instantiate --
# ScriptRegistry::TryCreate returns false, entities with a Behaviour attached
# just sit there doing nothing, with no visible error (Log has nowhere to
# print to on a real device). This is 3DS-only: on desktop, GameScripts is a
# real DLL, which always contains all of its own code regardless of what the
# loading process references, so this failure mode doesn't exist there.
# --whole-archive forces every member object file to be linked in
# unconditionally, matching what SHARED-library linking already does for
# free.
target_link_libraries(DualityPlayer PRIVATE
    DualityEngine
    -Wl,--whole-archive GameScripts -Wl,--no-whole-archive
)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    # Produces DualityPlayer.3dsx next to the .elf, using devkitPro's own
    # CMake helper (ships with the installed devkitARM-cmake package) --
    # default icon/name/description for now, customized in a later phase.
    #
    # ROMFS packages romfs/Scene.scene (a real scene saved from the Editor,
    # copied in manually for now -- an automatic Assets/-to-romfs cook step
    # is a later phase) so it's readable on-device as romfs:/Scene.scene.
    # Note: this plain-directory ROMFS form isn't dependency-tracked by
    # CMake, so a `cmake --build` won't notice romfs/ contents changing on
    # their own -- reconfigure/rebuild (build-3ds.bat always does a clean
    # build) to pick up an updated Scene.scene.
    ctr_create_3dsx(DualityPlayer ROMFS romfs)

    # .cia packaging (installable via FBI on real hardware/CFW). makerom and
    # bannertool are NOT part of devkitPro's own pacman repos (confirmed by
    # building devkitPro's own template sample -- it only ever produces
    # .3dsx) -- they're fetched from their official GitHub releases
    # (3DSGuy/Project_CTR and carstene1ns/3ds-bannertool, the recognized
    # community backup of the original Steveice10/bannertool) into Tools/.
    find_program(DE_MAKEROM_EXE makerom PATHS ${CMAKE_SOURCE_DIR}/Tools NO_DEFAULT_PATH)
    find_program(DE_BANNERTOOL_EXE bannertool PATHS ${CMAKE_SOURCE_DIR}/Tools NO_DEFAULT_PATH)

    if(DE_MAKEROM_EXE AND DE_BANNERTOOL_EXE)
        set(DE_PKG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Packaging)
        set(DE_CIA_TMP ${CMAKE_CURRENT_BINARY_DIR}/cia-tmp)
        set(DE_ICON_SMDH ${DE_CIA_TMP}/icon.smdh)
        set(DE_BANNER_BNR ${DE_CIA_TMP}/banner.bnr)
        set(DE_CIA_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/DualityPlayer.cia)

        add_custom_command(
            OUTPUT ${DE_ICON_SMDH}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${DE_CIA_TMP}
            COMMAND ${DE_BANNERTOOL_EXE} makesmdh -s "DualityEngine" -l "DualityEngine Sample" -p "DualityEngine"
                    -i ${DE_PKG_DIR}/icon.png -o ${DE_ICON_SMDH}
            DEPENDS ${DE_PKG_DIR}/icon.png
            COMMENT "Generating .cia icon (SMDH)"
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${DE_BANNER_BNR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${DE_CIA_TMP}
            COMMAND ${DE_BANNERTOOL_EXE} makebanner -i ${DE_PKG_DIR}/banner.png -a ${DE_PKG_DIR}/banner.wav -o ${DE_BANNER_BNR}
            DEPENDS ${DE_PKG_DIR}/banner.png ${DE_PKG_DIR}/banner.wav
            COMMENT "Generating .cia banner"
            VERBATIM
        )
        # -desc app:4 lets makerom fill in AccessControlInfo/exheader
        # defaults, so Packaging/template.rsf only needs BasicInfo/RomFs/
        # TitleInfo/Option -- fine for homebrew, not for anything needing
        # non-default permissions.
        add_custom_command(
            OUTPUT ${DE_CIA_OUTPUT}
            COMMAND ${DE_MAKEROM_EXE} -f cia -o ${DE_CIA_OUTPUT}
                    -rsf ${DE_PKG_DIR}/template.rsf -target t -exefslogo -desc app:4
                    -elf $<TARGET_FILE:DualityPlayer>
                    -icon ${DE_ICON_SMDH} -banner ${DE_BANNER_BNR}
                    "-DAPP_TITLE=DualityEngine Sample" -DAPP_ROMFS=${CMAKE_CURRENT_SOURCE_DIR}/romfs -DAPP_UNIQUE_ID=0xff3ff
                    -major 0 -minor 1 -micro 0
            DEPENDS DualityPlayer ${DE_ICON_SMDH} ${DE_BANNER_BNR} ${DE_PKG_DIR}/template.rsf
            COMMENT "Packaging .cia"
            VERBATIM
        )
        add_custom_target(DualityPlayer_cia ALL DEPENDS ${DE_CIA_OUTPUT})
    else()
        message(WARNING "DualityPlayer: makerom/bannertool not found under Tools/ -- .cia packaging skipped (only .3dsx will be built)")
    endif()
endif()
