
set ( ircp_SOURCES
  debug.h
  dirtraverse.c dirtraverse.h
  ircp.c        ircp.h
  ircp_client.c ircp_client.h
  ircp_io.c     ircp_io.h
  ircp_server.c ircp_server.h
)

add_executable ( ircp EXCLUDE_FROM_ALL ${ircp_SOURCES} )
target_link_libraries ( ircp openobex )
install ( PROGRAMS $<TARGET_FILE:ircp>
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT applications
  OPTIONAL
)
add_dependencies ( openobex-apps ircp )
