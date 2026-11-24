# elara: the Qt Quick desktop application. Qt draws, JUCE plays (ADR-0002);
# the app talks to audio only through elara::audio_juce's plain C++ API.
qt_standard_project_setup(REQUIRES 6.12
  I18N_SOURCE_LANGUAGE en
  I18N_TRANSLATED_LANGUAGES es)

# Painting of display lists and the list models the views show, in a library
# of their own so that tests link them without the QML application.
add_library(elara_app_view STATIC
  src/score_painter.cpp
  src/track_list.cpp)
target_include_directories(elara_app_view PUBLIC src)
target_link_libraries(elara_app_view PUBLIC Qt6::Gui elara::notation)
elara_source_warnings(src/score_painter.cpp src/track_list.cpp)

# The app icon (ADR-0064), drawn from icons/elara.svg at build time by a tool of
# its own: PNG images for the window, an .ico file for the Windows executable
# and an .icns file for the macOS bundle. Linux desktops take the SVG itself.
add_library(elara_app_icon STATIC src/app_icon.cpp)
target_include_directories(elara_app_icon PUBLIC src)
target_link_libraries(elara_app_icon PUBLIC Qt6::Gui Qt6::Svg)
elara_source_warnings(src/app_icon.cpp)

add_executable(elara_icon_files tools/icon_files.cpp)
set_target_properties(elara_icon_files PROPERTIES OUTPUT_NAME elara-icon-files)
target_link_libraries(elara_icon_files PRIVATE elara_app_icon)
elara_source_warnings(tools/icon_files.cpp)

set(ELARA_ICON_DIR "${CMAKE_CURRENT_BINARY_DIR}/icon")
set(_elara_icon_pngs)
foreach(size IN ITEMS 16 24 32 48 64 128 256)
  list(APPEND _elara_icon_pngs "${ELARA_ICON_DIR}/elara-${size}.png")
endforeach()
add_custom_command(
  OUTPUT ${_elara_icon_pngs} "${ELARA_ICON_DIR}/elara.ico" "${ELARA_ICON_DIR}/elara.icns"
  # On Windows the tool finds the Qt libraries on PATH.
  COMMAND "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "$<TARGET_FILE:elara_icon_files>" "${CMAKE_CURRENT_SOURCE_DIR}/icons/elara.svg"
    "${ELARA_ICON_DIR}"
  DEPENDS elara_icon_files icons/elara.svg
  COMMENT "Drawing the app icon"
  VERBATIM)

qt_add_executable(elara WIN32 MACOSX_BUNDLE
  src/main.cpp)

# macOS asks the user before an app may listen to the microphone (the tuner,
# ADR-0029), and quits an app whose Info.plist does not say why it wants to.
set_target_properties(elara PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.ghost-in-the-groove.elara"
  MACOSX_BUNDLE_BUNDLE_NAME "Elara"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

# The theme is one object shared by every QML file.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(elara
  URI Elara.App
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/Elevation.qml
    qml/ScoreView.qml
    qml/EntryBar.qml
    qml/MeasurePopup.qml
    qml/SignatureFields.qml
    qml/WelcomePane.qml
    qml/SegmentedControl.qml
    qml/IconButton.qml
    qml/Icon.qml
    qml/Logo.qml
    qml/MusicSymbol.qml
    qml/TunerPane.qml
    qml/SongPane.qml
    qml/TuningFields.qml
    qml/PracticeControls.qml
  SOURCES
    src/audio_controller.hpp
    src/audio_controller.cpp
    src/score_document.hpp
    src/score_document.cpp
    src/score_page.hpp
    src/score_page.cpp
    src/startup.hpp
    src/startup.cpp
    src/tuner_controller.hpp
    src/tuner_controller.cpp)

# Translations of the UI strings (qsTr/tr), compiled into the executable under
# :/i18n. The `update_translations` target runs lupdate to refresh the .ts
# files from the sources; each build compiles them with lrelease.
qt_add_translations(elara
  SOURCE_TARGETS elara
  TS_FILE_DIR i18n
  TS_FILE_BASE elara
  RESOURCE_PREFIX /i18n
  LUPDATE_OPTIONS -no-obsolete -locations none)

# The fonts scores are painted with, and the controls style, built into the
# executable so that it finds them wherever it is installed.
qt_add_resources(elara "fonts"
  PREFIX "/fonts"
  BASE "${ELARA_FONT_DIR}"
  FILES
    "${ELARA_FONT_DIR}/Bravura.otf"
    "${ELARA_FONT_DIR}/Bravura.json"
    "${ELARA_FONT_DIR}/Arimo.ttf"
    "${ELARA_FONT_DIR}/Arimo-Italic.ttf")
qt_add_resources(elara "style"
  PREFIX "/"
  FILES qtquickcontrols2.conf)
# The window's icon, at each size up to 256 pixels.
qt_add_resources(elara "icon"
  PREFIX "/icons"
  BASE "${ELARA_ICON_DIR}"
  FILES ${_elara_icon_pngs})

# The icon and version of the executable on Windows, and the icon of the bundle
# on macOS.
if(WIN32)
  set(_elara_icon_ico "${ELARA_ICON_DIR}/elara.ico")
  configure_file(elara.rc.in "${CMAKE_CURRENT_BINARY_DIR}/elara.rc" @ONLY)
  target_sources(elara PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/elara.rc")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/elara.rc" PROPERTIES
    OBJECT_DEPENDS "${_elara_icon_ico}")
elseif(APPLE)
  set_source_files_properties("${ELARA_ICON_DIR}/elara.icns" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  target_sources(elara PRIVATE "${ELARA_ICON_DIR}/elara.icns")
  set_target_properties(elara PROPERTIES MACOSX_BUNDLE_ICON_FILE elara.icns)
endif()

# The generated QML type registration includes each QML_ELEMENT header by its
# bare file name, so the directory holding those headers must be on the path.
target_include_directories(elara PRIVATE src)

target_link_libraries(elara PRIVATE
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Concurrent
  elara_app_view
  elara::io
  elara::notation
  elara::core
  elara::audio_juce
  elara::audio_fluid)

# Strict warnings for our sources only, not for moc/qmlcache output.
elara_source_warnings(src/main.cpp src/audio_controller.cpp src/score_document.cpp
  src/score_page.cpp src/startup.cpp src/tuner_controller.cpp)

# Installing (ADR-0048): the app, then the Qt libraries, plugins and QML modules
# it uses, found by Qt's deployment (windeployqt, macdeployqt, or Qt's own on
# Linux). On Windows and Linux the offscreen platform plugin comes too, so a
# package can be smoke-tested without a screen (macOS uses its cocoa plugin).
# On Windows Qt's own translations come only for the languages the app speaks,
# and the compiler runtime as DLLs (see ElaraPackaging.cmake), not as its 25 MB
# installer.
install(TARGETS elara
  BUNDLE DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32)
  install(FILES $<TARGET_RUNTIME_DLLS:elara> DESTINATION "${CMAKE_INSTALL_BINDIR}")
  set(_elara_deploy_options INCLUDE_PLUGINS qoffscreen NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS "--translations;en,es")
elseif(NOT APPLE AND TARGET Qt6::QOffscreenIntegrationPlugin)
  # Qt's Linux deployment brings only the default platform plugin (xcb).
  install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin> DESTINATION plugins/platforms)
endif()
if(NOT WIN32 AND NOT APPLE)
  # The desktop entry and its icon, named after the app's id (ADR-0064).
  set(_elara_app_id io.github.ghost_in_the_groove.Elara)
  install(FILES elara.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    RENAME "${_elara_app_id}.desktop")
  install(FILES icons/elara.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME "${_elara_app_id}.svg")
  install(FILES "${ELARA_ICON_DIR}/elara-256.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
    RENAME "${_elara_app_id}.png")
endif()
qt_generate_deploy_qml_app_script(
  TARGET elara
  OUTPUT_SCRIPT _elara_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  ${_elara_deploy_options})
install(SCRIPT "${_elara_deploy_script}")
