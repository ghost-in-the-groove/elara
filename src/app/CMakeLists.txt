# elara: the Qt Quick desktop application. Qt draws, JUCE plays (ADR-0002);
# the app talks to audio only through elara::audio_juce's plain C++ API.
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(elara WIN32 MACOSX_BUNDLE
  src/main.cpp)

qt_add_qml_module(elara
  URI Elara.App
  VERSION 1.0
  QML_FILES
    qml/Main.qml
  SOURCES
    src/audio_controller.hpp
    src/audio_controller.cpp)

# The generated QML type registration includes each QML_ELEMENT header by its
# bare file name, so the directory holding those headers must be on the path.
target_include_directories(elara PRIVATE src)

target_link_libraries(elara PRIVATE
  Qt6::Quick
  Qt6::QuickControls2
  elara::core
  elara::audio_juce)

# Strict warnings for our sources only, not for moc/qmlcache output.
elara_source_warnings(src/main.cpp src/audio_controller.cpp)
