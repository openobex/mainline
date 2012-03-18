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

  # If there is a udev.pc pkg-config file in the system, use it.
  if (NOT CMAKE_INSTALL_UDEVRULESDIR)
    set ( UDEVDIR /lib/udev )
    find_package ( PkgConfig )
    if ( PKG_CONFIG_FOUND )
      pkg_check_modules ( PKGCONFIG_UDEV udev )
      if ( PKGCONFIG_UDEV_FOUND )
	execute_process(
	  COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=udevdir udev
	  OUTPUT_VARIABLE PKGCONFIG_UDEVDIR
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
        if ( PKGCONFIG_UDEVDIR )
	  file ( TO_CMAKE_PATH "${PKGCONFIG_UDEVDIR}" UDEVDIR )
        endif ( PKGCONFIG_UDEVDIR )
      endif ( PKGCONFIG_UDEV_FOUND )
    endif ( PKG_CONFIG_FOUND )
  endif (NOT CMAKE_INSTALL_UDEVRULESDIR)
  set ( CMAKE_INSTALL_UDEVRULESDIR ${UDEVDIR}/rules.d
        CACHE PATH "Where to install udev rules" )
  mark_as_advanced ( CMAKE_INSTALL_UDEVRULESDIR )

  install ( FILES ${CMAKE_CURRENT_BINARY_DIR}/openobex.rules
    DESTINATION ${CMAKE_INSTALL_UDEVRULESDIR}
    COMPONENT system
    RENAME 60-openobex.rules
  )
endif ( CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENOBEX_USB )
