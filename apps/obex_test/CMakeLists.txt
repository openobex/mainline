
set ( obex_test_SOURCES
  obex_test.c obex_test.h
  obex_test_client.c obex_test_client.h
  obex_test_server.c obex_test_server.h
)

add_executable ( obex_test EXCLUDE_FROM_ALL
  ${obex_test_SOURCES}
)

target_link_libraries ( obex_test
  openobex-apps-common
  openobex
  ${Bluetooth_LIBRARIES}
)

if ( WIN32 )
  target_link_libraries ( obex_test ws2_32 )
endif ( WIN32 )

install ( PROGRAMS $<TARGET_FILE:obex_test>
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT applications
  OPTIONAL
)

add_dependencies ( openobex-apps obex_test )
