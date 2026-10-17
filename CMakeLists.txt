# Keep the old provider for other builds. Only Sofle opts into the new screen.
if(CONFIG_SOFLE_STATUS_SCREEN)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_status_screen.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_status_render.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_union_logo.c
    ${CMAKE_CURRENT_LIST_DIR}/src/oled_toggle_minimal.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_display_protocol.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_display_sync.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sofle_zoom_plan.c)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/behavior_profile_zoom.c)
  endif()
elseif(CONFIG_ZMK_BEHAVIOR_OLED_TOGGLE AND CONFIG_DISPLAY)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/oled_toggle.c)
endif()
