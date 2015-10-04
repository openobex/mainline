
add_subdirectory ( lib )
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/lib )
add_subdirectory ( obex_test )
add_subdirectory ( ircp )

set ( OPENOBEX_COMMON_APPS
  irxfer
  irobex_palm3
)
set ( OPENOBEX_APPS
  obex_find
)

if ( NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" )
  #obex_tcp uses functions that are only available
  #under Windows7, so we do not compile for now.
  list ( APPEND OPENOBEX_COMMON_APPS obex_tcp )
endif ( NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" )

foreach ( prog ${OPENOBEX_COMMON_APPS} )
  list ( APPEND ${prog}_LIBS openobex-apps-common )
  list ( APPEND OPENOBEX_APPS ${prog} )
endforeach ( prog )

foreach ( prog ${OPENOBEX_APPS} )
  set ( ${prog}_SOURCES ${prog}.c )
  list ( APPEND ${prog}_LIBS openobex )
endforeach ( prog )

if ( WIN32 )
  list ( APPEND obex_tcp_LIBS ws2_32 )
endif ( WIN32 )

foreach ( prog ${OPENOBEX_APPS} )
  add_executable ( ${prog} EXCLUDE_FROM_ALL ${${prog}_SOURCES} )
  target_link_libraries ( ${prog} ${${prog}_LIBS} )
  install ( PROGRAMS $<TARGET_FILE:${prog}>
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT applications
    OPTIONAL
  )
endforeach ( prog )
add_dependencies ( openobex-apps ${OPENOBEX_APPS} )
