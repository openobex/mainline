if ( CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENOBEX_USB )
  add_executable ( obex-check-device obex-check-device.c )
  target_link_libraries ( obex-check-device openobex )
  install ( TARGETS obex-check-device
    DESTINATION sbin
    COMPONENT system
  )

  # The udev rules file needs to be configured to have the right
  # path to obex-check-device.
  configure_file ( openobex.rules.in openobex.rules @ONLY )
  set ( UDEV_RULES_INSTALL_DIR /lib/udev/rules.d
        CACHE PATH "Where to install udev rules" )
  mark_as_advanced ( UDEV_RULES_INSTALL_DIR )
  install ( FILES ${CMAKE_CURRENT_BINARY_DIR}/openobex.rules
    DESTINATION ${UDEV_RULES_INSTALL_DIR}
    COMPONENT system
    RENAME 60-openobex.rules
  )
endif ( CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENOBEX_USB )
