# SPDX-License-Identifier: GPL-2.0-or-later
file(GLOB _FILES "README" "*.svg" "*.jpg" "*.png")
install(FILES ${_FILES} DESTINATION ${INKSCAPE_SHARE_INSTALL}/tutorials)
