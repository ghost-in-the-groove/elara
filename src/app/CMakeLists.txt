# elara: the Qt Quick desktop application. Qt draws, JUCE plays (ADR-0002);
# the app talks to audio only through elara::audio_juce's plain C++ API.
qt_standard_project_setup(REQUIRES 6.12)

# Painting of display lists, in a library of its own so that tests link it
# without the QML application.
add_library(elara_app_view STATIC
  src/score_painter.cpp)
target_include_directories(elara_app_view PUBLIC src)
target_link_libraries(elara_app_view PUBLIC Qt6::Gui elara::notation)
elara_source_warnings(src/score_painter.cpp)

qt_add_executable(elara WIN32 MACOSX_BUNDLE
  src/main.cpp)

qt_add_qml_module(elara
  URI Elara.App
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/ScoreView.qml
    qml/WelcomePane.qml
    qml/SegmentedControl.qml
    qml/IconButton.qml
        qml/Icon.qml
    qml/MusicSymbol.qml
  SOURCES
    src/audio_controller.hpp
    src/audio_controller.cpp
    src/score_document.hpp
    src/score_document.cpp
    src/score_page.hpp
    src/score_page.cpp
    src/startup.hpp
    src/startup.cpp)

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
  src/score_page.cpp src/startup.cpp)
