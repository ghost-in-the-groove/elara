# SPDX-License-Identifier: AGPL-3.0-or-later
# Puts the file FROM into the folder TO, where the app looks for the SoundFont
# that comes with it (ADR-0069), or removes TO when FROM is not given.
if(DEFINED FROM)
  file(COPY "${FROM}" DESTINATION "${TO}")
else()
  file(REMOVE_RECURSE "${TO}")
endif()
