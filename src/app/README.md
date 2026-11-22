# elara (desktop app)

Qt Quick / QML user interface.

- **May depend on:** Qt, every Elara module. **Must not include:** JUCE (go through
  `elara::audio_juce`'s plain C++ API).
- QML lives in `qml/`, C++ glue (QObjects exposed to QML) in `src/`.
- UI strings are English source strings wrapped in `qsTr()` / `tr()`; Spanish and
  English translations are added in milestone 5 (see `docs/roadmap.md`).
- `elara --smoke-test` loads the QML, fails on any QML warning and exits. CTest
  runs it with `QT_QPA_PLATFORM=offscreen` (`app.smoke`), so CI can check the UI
  loads without a screen.
