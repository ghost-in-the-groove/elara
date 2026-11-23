# 0030. Translations of the user interface

- Status: Accepted
- Date: 2026-11-23

## Context

Elara is written in English and its first users speak Spanish too (SCOPE.md).
The UI strings have been English source strings wrapped in `qsTr()` and
`tr()` from the start. What remained was how the translations are kept,
shipped and chosen. A string added without its translation should not reach
a release unnoticed, and CI has no one to look at the window (ADR-0003).

## Decision

**Qt Linguist files in the sources.** The translations live as Qt Linguist
`.ts` files in `src/app/i18n/`, one per language: `elara_es.ts` and
`elara_en.ts`. The source language is English (`I18N_SOURCE_LANGUAGE en`), so
`elara_en.ts` holds only plural forms (`%n measure(s)` → "1 measure",
"2 measures"). `python scripts/elara.py translations` runs lupdate over the
app's own sources (`qt_add_translations(... SOURCE_TARGETS elara)`, not
JUCE's) and refreshes the files, without line numbers so that moving code does
not touch them. Translators edit them in Qt Linguist or by hand.

**Compiled into the executable.** Each build compiles the files with lrelease
into `:/i18n/elara_<language>.qm`, so the app finds them wherever it is
installed, as it finds its fonts.

**The system's language, or the one asked for.** At start the app loads the
catalog for the first of the system's UI languages that it has, and English
otherwise. `--language es` (or `en`) chooses one; a language without a
catalog is refused. The language is read before the command line is parsed,
so that the help text is translated too. Qt's own strings come from Qt's
catalogs when they are installed next to Qt; nothing fails without them.

**Complete or failing.** `app.translations_complete` refreshes a copy of each
`.ts` file from the sources and fails when the copy differs from the
committed file (a string added, changed or removed without refreshing) or
when a translation is unfinished or empty. `app.smoke_spanish` loads the
window in Spanish and checks that the catalog loaded.

Terms follow `docs/glossary.md` (`pista`, `compás`, `afinación`). Note names
stay letters (E, A, D...), as in tablature and chord charts; solfège names
(Mi, La, Re) would be a setting of their own.

## Consequences

- A change that adds a UI string must translate it in the same change, or
  the gate fails. This keeps the languages complete at the cost of a step per
  string; `elara.py translations` makes it one command.
- Messages that come from the core libraries (an import error, a device
  name) are not translated: they are English `std::string`s, and the modules
  below the app have no Qt. Translating them would need message codes in
  those modules.
- Another language is one more `.ts` file in `I18N_TRANSLATED_LANGUAGES`, and
  `--language` accepts it once its catalog exists.
