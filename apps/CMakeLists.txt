
add_subdirectory ( lib )
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/lib )
add_subdirectory ( obex_test )

set ( OPENOBEX_COMMON_APPS
  irxfer
  obex_tcp
  irobex_palm3
)

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
  get_target_property ( ${prog}_EXECUTABLE ${prog} LOCATION )
  install ( PROGRAMS ${${prog}_EXECUTABLE}
    DESTINATION bin
    COMPONENT applications
    OPTIONAL
  )
endforeach ( prog )
add_dependencies ( openobex-apps ${OPENOBEX_APPS} )
