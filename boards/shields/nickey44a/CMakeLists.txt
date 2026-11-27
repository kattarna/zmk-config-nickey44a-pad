if(CONFIG_ZMK_BEHAVIOR_F13_GESTURE_GATE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/f13_gesture_gate.c)
endif()
