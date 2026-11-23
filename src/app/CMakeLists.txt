# elara: the Qt Quick desktop application. Qt draws, JUCE plays (ADR-0002);
# the app talks to audio only through elara::audio_juce's plain C++ API.
qt_standard_project_setup(REQUIRES 6.12
  I18N_SOURCE_LANGUAGE en
  I18N_TRANSLATED_LANGUAGES es)

# Painting of display lists, in a library of its own so that tests link it
# without the QML application.
add_library(elara_app_view STATIC
  src/score_painter.cpp)
target_include_directories(elara_app_view PUBLIC src)
target_link_libraries(elara_app_view PUBLIC Qt6::Gui elara::notation)
elara_source_warnings(src/score_painter.cpp)

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

qt_add_qml_module(elara
  URI Elara.App
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/ScoreView.qml
    qml/EntryBar.qml
    qml/WelcomePane.qml
    qml/SegmentedControl.qml
    qml/IconButton.qml
        qml/Icon.qml
    qml/MusicSymbol.qml
    qml/TunerPane.qml
    qml/SongPane.qml
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
  elara::audio_juce)

# Strict warnings for our sources only, not for moc/qmlcache output.
elara_source_warnings(src/main.cpp src/audio_controller.cpp src/score_document.cpp
  src/score_page.cpp src/startup.cpp src/tuner_controller.cpp)

# Installing (ADR-0048): the app, then the Qt libraries, plugins and QML modules
# it uses, found by Qt's deployment (windeployqt, macdeployqt, or Qt's own on
# Linux). On Windows the offscreen platform plugin comes too, so a package can
# be smoke-tested without a screen, and Qt's own translations only for the
# languages the app speaks; the compiler runtime comes as DLLs (see
# ElaraPackaging.cmake), not as its 25 MB installer.
install(TARGETS elara
  BUNDLE DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32)
  install(FILES $<TARGET_RUNTIME_DLLS:elara> DESTINATION "${CMAKE_INSTALL_BINDIR}")
  set(_elara_deploy_options INCLUDE_PLUGINS qoffscreen NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS "--translations;en,es")
endif()
qt_generate_deploy_qml_app_script(
  TARGET elara
  OUTPUT_SCRIPT _elara_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  ${_elara_deploy_options})
install(SCRIPT "${_elara_deploy_script}")
