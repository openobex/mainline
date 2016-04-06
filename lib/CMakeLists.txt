#
# Main library version and shared object version
#

# this defines supported properties that are set from
# variables of the form openobex_*
set ( openobex_PROPERTIES
  VERSION
  SOVERSION
  COMPILE_DEFINITIONS
  LINK_FLAGS
)

# the ABI version, must be increased on incompatible changes
set ( openobex_SOVERSION "2" )


set ( SOURCES
  api.c
  obex_client.c
  obex_connect.c
  obex_hdr.c
  obex_hdr_membuf.c
  obex_hdr_ptr.c
  obex_hdr_stream.c
  obex_body.c
  obex_main.c
  obex_msg.c
  obex_object.c
  obex_server.c
  obex_transport.c
  obex_transport_sock.c
  databuffer.c
  membuf.c
  transport/inobex.c
  transport/fdobex.c
  transport/customtrans.c
)

include_directories (
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)
set ( HEADERS
  obex_client.h
  obex_connect.h
  obex_hdr.h
  obex_body.h
  obex_main.h
  obex_msg.h
  obex_object.h
  obex_server.h
  obex_transport.h
  databuffer.h
  membuf.h
  debug.h
  defines.h
  obex_incl.h
  cloexec.h
  nonblock.h
  transport/inobex.h
  transport/fdobex.h
  transport/customtrans.h
)

if ( CMAKE_SYSTEM_NAME STREQUAL "Linux" )
  # Activates some functions not defined without
  add_definitions ( -D_GNU_SOURCE )
endif ( CMAKE_SYSTEM_NAME STREQUAL "Linux" )

if ( NOT OBEX_DEBUG )
  set ( OBEX_DEBUG 0 CACHE STRING "Amount of debug message (1-4)" )
endif ( NOT OBEX_DEBUG )
list ( APPEND openobex_COMPILE_DEFINITIONS OBEX_DEBUG=${OBEX_DEBUG} )

if ( NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" )
  option ( OBEX_DEBUG_SYSLOG "Use SysLog facility instead of stderr for debug messages" )
  if ( OBEX_DEBUG_SYSLOG )
    list ( APPEND openobex_COMPILE_DEFINITIONS OBEX_DEBUG_SYSLOG )
  endif ( OBEX_DEBUG_SYSLOG )
endif ( NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" )

if ( NOT OBEX_DUMP )
  set ( OBEX_DUMP 0 CACHE STRING "Tx/Rx message dump" )
endif ( NOT OBEX_DUMP )
list ( APPEND openobex_COMPILE_DEFINITIONS OBEX_DUMP=${OBEX_DUMP} )

if ( OPENOBEX_IRDA )
  list ( APPEND SOURCES
    transport/irobex.c
  )
  list ( APPEND HEADERS
    transport/irobex.h
    transport/irda_wrap.h
  )
endif ( OPENOBEX_IRDA )

if ( OPENOBEX_BLUETOOTH )
  list ( APPEND SOURCES
    transport/btobex.c
  )
  list ( APPEND HEADERS
    transport/btobex.h
    transport/bluez_compat.h
  )
endif ( OPENOBEX_BLUETOOTH )

if ( OPENOBEX_USB )
  list ( APPEND openobex_LIBRARIES
    ${LibUSB_LIBRARIES}
  )
  include_directories ( SYSTEM ${LibUSB_INCLUDE_DIRS} )
  if ( LibUSB_VERSION_1.0 )
    list ( APPEND SOURCES
      transport/usb1obex.c
      transport/usbutils.c
    )
  else ( LibUSB_VERSION_1.0 )
    list ( APPEND SOURCES
      transport/usbobex.c
      transport/usbutils.c
    )
  endif ( LibUSB_VERSION_1.0 )
  list ( APPEND HEADERS
    transport/usbobex.h
    transport/usbutils.h
  )
endif ( OPENOBEX_USB )

set ( openobex_LINK_FLAGS "${openobex_LINK_FLAGS} ${LINKER_FLAG_NOUNDEFINED}" )

if ( WIN32 )
  if ( CMAKE_COMPILER_IS_GNUCC )
    set ( openobex_LINK_FLAGS
      "${openobex_LINK_FLAGS} -Wl,--disable-stdcall-fixup -Wl,--add-stdcall-alias"
    )
  endif ( CMAKE_COMPILER_IS_GNUCC )

  list ( APPEND openobex_LIBRARIES
    ws2_32
  )

  if ( CMAKE_RC_COMPILER )
    set ( OPENOBEX_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/openobex.rc" )
    configure_file (
      "${CMAKE_CURRENT_SOURCE_DIR}/openobex.rc.in"
      "${OPENOBEX_RC_FILE}"
      @ONLY
    )
  endif ( CMAKE_RC_COMPILER )

  if ( MSVC )
    set ( OPENOBEX_DEF_FILE "${CMAKE_CURRENT_BINARY_DIR}/openobex.def" )
    file ( WRITE "${OPENOBEX_DEF_FILE}" "VERSION ${openobex_VERSION_MAJOR}.${openobex_VERSION_MINOR}\n" )
    file ( APPEND "${OPENOBEX_DEF_FILE}" "EXPORTS\n" )
    file ( READ "${CMAKE_CURRENT_SOURCE_DIR}/obex.sym" OPENOBEX_SYMBOLS )
    file ( APPEND "${OPENOBEX_DEF_FILE}" "${OPENOBEX_SYMBOLS}\n" )

    # MSVC <= 7.1 needs some special tricks
    if ( MSVC_VERSION LESS "1400" )
      list ( APPEND SOURCES win32compat.c )
    endif ( MSVC_VERSION LESS "1400" )
  endif ( MSVC )
endif ( WIN32 )

if ( CYGWIN )
  #also define _WIN32 under CygWin
  list ( APPEND openobex_COMPILE_DEFINITIONS _WIN32)
endif ( CYGWIN )

# Add the openobex library target
add_library ( openobex
  ${SOURCES}
  ${HEADERS}
  ${openobex_PUBLIC_HEADERS}
  ${OPENOBEX_RC_FILE}
  ${OPENOBEX_DEF_FILE}
)

target_link_libraries ( openobex
  PRIVATE
    ${openobex_LIBRARIES}
)

foreach ( i ${openobex_PROPERTIES} )
  if ( DEFINED openobex_${i} )
    set_property ( TARGET openobex PROPERTY ${i} ${openobex_${i}} )
  endif ( DEFINED openobex_${i} )
endforeach ( i )

generate_export_header( openobex )

install ( TARGETS openobex
  EXPORT openobex-target
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT library
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT library
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT devel
)

#
# Create the openobex-config file for the build tree
#
export ( TARGETS openobex
  FILE ${CMAKE_CURRENT_BINARY_DIR}/openobex-build.cmake
)
configure_file (
  ${CMAKE_CURRENT_SOURCE_DIR}/openobex-build-settings.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/openobex-build-settings.cmake
  @ONLY
)

#
# Create and copy the openobex-config.cmake files for the installed copy
#
set ( CMAKE_INSTALL_CMAKEBASEDIR ${CMAKE_INSTALL_LIBDIR}/cmake
      CACHE PATH "Where to install the cmake config files" )

file(RELATIVE_PATH CMAKE_INSTALL_REL_INCLUDEDIR
  ${CMAKE_INSTALL_FULL_LIBDIR}/cmake/OpenObex
  ${CMAKE_INSTALL_FULL_INCLUDEDIR}
)
configure_file (
  ${PROJECT_SOURCE_DIR}/openobex-config.cmake.in
  ${PROJECT_BINARY_DIR}/openobex-config.cmake
  @ONLY
)
configure_file (
  ${PROJECT_SOURCE_DIR}/openobex-config-version.cmake.in
  ${PROJECT_BINARY_DIR}/openobex-config-version.cmake
  @ONLY
)

install ( FILES
  ${PROJECT_BINARY_DIR}/openobex-config.cmake
  ${PROJECT_BINARY_DIR}/openobex-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_CMAKEBASEDIR}/OpenObex-${openobex_VERSION}
  COMPONENT devel
)
install ( EXPORT openobex-target
  DESTINATION ${CMAKE_INSTALL_CMAKEBASEDIR}/OpenObex-${openobex_VERSION}
  COMPONENT devel
)

#
# Copy the .pc file to install it only if the lib gets installed
#
add_custom_command ( TARGET openobex
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E copy_if_different ${PROJECT_BINARY_DIR}/openobex.pc
          ${CMAKE_CURRENT_BINARY_DIR}/openobex.pc
  VERBATIM
)
install ( FILES ${CMAKE_CURRENT_BINARY_DIR}/openobex.pc
  DESTINATION ${PKGCONFIG_INSTALL_DIR}
  COMPONENT devel
  OPTIONAL
)


#  include ( GetPrerequisites )
#  get_prerequisites ( openobex CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS 0 1 )

# By default, do not warn when built on machines using only VS Express:
if ( NOT DEFINED CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS )
  set ( CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS ON )
endif ( NOT DEFINED CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS )
include ( InstallRequiredSystemLibraries )
