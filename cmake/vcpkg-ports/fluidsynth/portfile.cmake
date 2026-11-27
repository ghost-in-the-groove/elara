# FluidSynth for Elara (ADR-0043): the synthesizer only. Elara plays audio
# through its own backend, so every audio and MIDI driver, the shell and the
# optional system libraries stay off; libsndfile with Ogg/Vorbis reads SF3.
# Based on the vcpkg port of the same version (MIT); fix-gcem.patch is its.
# share-soundfonts.patch lets synthesizers on different threads play one
# loaded SoundFont (ADR-0146).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FluidSynth/fluidsynth
    REF "v${VERSION}"
    SHA512 79420eb33695dc036c143f68574c73b897c48be0f9c54115b4fc313cda9e6d60909a8044522027746c259aa6a0fdb0d58ab456fa991c4b4714dffcd5232e063d
    HEAD_REF master
    PATCHES
        fix-gcem.patch
        share-soundfonts.patch
)
# Use the dependencies' own CMake packages, not FluidSynth's find modules.
file(REMOVE
    "${SOURCE_PATH}/cmake_admin/FindFLAC.cmake"
    "${SOURCE_PATH}/cmake_admin/Findmp3lame.cmake"
    "${SOURCE_PATH}/cmake_admin/Findmpg123.cmake"
    "${SOURCE_PATH}/cmake_admin/FindOgg.cmake"
    "${SOURCE_PATH}/cmake_admin/FindOpus.cmake"
    "${SOURCE_PATH}/cmake_admin/FindSndFileLegacy.cmake"
    "${SOURCE_PATH}/cmake_admin/FindVorbis.cmake"
    "${SOURCE_PATH}/cmake_admin/FindGCEM.cmake"
)

set(DISABLED_OPTIONS
    enable-alsa enable-aufile enable-coreaudio enable-coremidi enable-coverage enable-dbus
    enable-dsound enable-floats enable-fpe-check enable-framework enable-ipv6 enable-jack
    enable-ladspa enable-libinstpatch enable-midishare enable-network enable-oboe enable-openmp
    enable-opensles enable-oss enable-pipewire enable-portaudio enable-profiling
    enable-pulseaudio enable-readline enable-sdl3 enable-signalsmith enable-systemd
    enable-trap-on-fpe enable-ubsan enable-wasapi enable-waveout enable-winmidi)
set(OPTIONS "-Denable-libsndfile:BOOL=ON" "-Dosal=cpp11" -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON)
foreach(_option IN LISTS DISABLED_OPTIONS)
    list(APPEND OPTIONS "-D${_option}:BOOL=OFF")
endforeach()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${OPTIONS}
    MAYBE_UNUSED_VARIABLES ${DISABLED_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/fluidsynth)
vcpkg_fixup_pkgconfig()

vcpkg_copy_tools(TOOL_NAMES fluidsynth AUTO_CLEAN)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/man")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
