# Root module CMake: compiles local ZMK sources for both halves.
if(CONFIG_ZMK_BEHAVIOR_OLED_TOGGLE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/oled_toggle.c)
endif()
