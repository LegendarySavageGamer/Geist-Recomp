cmake_minimum_required(VERSION 3.20)
project(GeistNative C CXX)

# Shared GameCube recompilation layer (MIT, Ethan Nicholas) -- pinned as a subdirectory.
add_subdirectory(gcn-recomp)

# Geist (USA) GITE01 rev 0. The DOL hash pins the exact build the tables were written for.
gcn_add_game(
  NAME geist
  TITLE "Geist"
  LABEL "GeistNative"
  DISC_ID GITE01
  DOL_SHA1 6612f52ce6c50bb831ffddd5264b22973304f288
  SYMBOLS ${CMAKE_CURRENT_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_CURRENT_SOURCE_DIR}/recomp
  IMAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/orig
  FPS 60
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu_preference.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/geist_romfont.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/geist_cheats.cpp
)
