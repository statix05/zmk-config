if(CONFIG_ZMK_BEHAVIOR_OLED_TOGGLE)
  zephyr_library()
  zephyr_library_sources(src/oled_toggle.c)
endif()
