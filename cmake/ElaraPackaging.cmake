# Installing and packaging (ADR-0048). The app and the CLI install with what
# they need at run time: the app with the Qt libraries, plugins and QML modules
# Qt's deployment tools find (src/app), the CLI with the fonts it renders with,
# and both with the licences. CPack turns the result into a package for the
# platform: a ZIP archive on Windows, a disk image on macOS, a tarball on Linux,
# and an installer where its tools are found: an MSI package on Windows (WiX
# Toolset 3); `elara.py package` makes the Linux AppImage itself (ADR-0065).
# `python scripts/elara.py package` builds them and smoke-tests what they install.

# Licences and notices, at the top of the package (Linux: the doc directory).
if(APPLE OR WIN32)
  set(ELARA_INSTALL_DOC_DIR ".")
else()
  set(ELARA_INSTALL_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION "${ELARA_INSTALL_DOC_DIR}")

# The fonts and their licences next to elara-cli, which renders SVG with them
# (the app has them built in).
install(DIRECTORY "${ELARA_FONT_DIR}/" DESTINATION "${ELARA_INSTALL_TOOLS_DIR}/fonts")

# The Visual C++ runtime DLLs next to the executables (Windows 10 and later
# have the universal CRT).
if(WIN32)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
  include(InstallRequiredSystemLibraries)
endif()

# vcpkg builds its libraries as DLLs on Windows, and $<TARGET_RUNTIME_DLLS>
# names only the ones Elara links to, not what they load in turn (FluidSynth
# loads libsndfile, which loads its codecs). Every DLL vcpkg built goes next to
# the executables instead; other platforms link vcpkg's libraries statically.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  set(_elara_vcpkg_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(APPEND _elara_vcpkg_dir "/debug")
  endif()
  file(GLOB _elara_vcpkg_dlls "${_elara_vcpkg_dir}/bin/*.dll")
  install(FILES ${_elara_vcpkg_dlls} DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(WIN32)
  set(_elara_platform "windows-x64")
elseif(APPLE)
  set(_elara_platform "macos-${CMAKE_SYSTEM_PROCESSOR}")
else()
  set(_elara_platform "linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(CPACK_PACKAGE_NAME "Elara")
set(CPACK_PACKAGE_VENDOR "Ghost in the Groove")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ghost-in-the-groove/elara")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Elara")
set(CPACK_PACKAGE_FILE_NAME "Elara-${PROJECT_VERSION}-${_elara_platform}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
  # The MSI installer (ADR-0065): per machine, in Program Files, with a Start
  # menu shortcut and the app icon in Apps and features. The upgrade code names
  # the product across versions and must never change.
  find_program(ELARA_WIX_CANDLE candle PATHS "$ENV{WIX}/bin" NO_CACHE)
  if(ELARA_WIX_CANDLE)
    list(APPEND CPACK_GENERATOR WIX)
  else()
    message(STATUS "WiX Toolset 3 not found: no MSI installer")
  endif()
  configure_file(LICENSE "${PROJECT_BINARY_DIR}/LICENSE.txt" COPYONLY)
  set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/LICENSE.txt")
  set(CPACK_PACKAGE_EXECUTABLES elara Elara)
  set(CPACK_WIX_UPGRADE_GUID "3B2C1D0B-59E3-4366-9E22-7BF1805A3DFB")
  set(CPACK_WIX_PRODUCT_ICON "${PROJECT_BINARY_DIR}/src/app/icon/elara.ico")
  set(CPACK_WIX_PROGRAM_MENU_FOLDER "Elara")
  set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_WIX_PROPERTY_ARPHELPLINK "${CPACK_PACKAGE_HOMEPAGE_URL}/issues")
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "Elara ${PROJECT_VERSION}")
  set(CPACK_DMG_FORMAT UDBZ)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
