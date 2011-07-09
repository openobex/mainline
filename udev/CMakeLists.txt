if ( CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENOBEX_USB )
  add_executable ( obex-check-device obex-check-device.c )
  target_link_libraries ( obex-check-device openobex )
  install ( TARGETS obex-check-device
    DESTINATION ${CMAKE_INSTALL_SBINDIR}
    COMPONENT system
  )

  # The udev rules file needs to be configured to have the right
  # path to obex-check-device.
  configure_file ( openobex.rules.in openobex.rules @ONLY )
  set ( CMAKE_INSTALL_UDEVRULESDIR /lib/udev/rules.d
        CACHE PATH "Where to install udev rules" )
  mark_as_advanced ( CMAKE_INSTALL_UDEVRULESDIR )
  install ( FILES ${CMAKE_CURRENT_BINARY_DIR}/openobex.rules
    DESTINATION ${CMAKE_INSTALL_UDEVRULESDIR}
    COMPONENT system
    RENAME 60-openobex.rules
  )
endif ( CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENOBEX_USB )
