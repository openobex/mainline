
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/.. )
add_executable ( obex_test EXCLUDE_FROM_ALL
  obex_test.c obex_test.h
  obex_test_client.c obex_test_client.h
  obex_test_server.c obex_test_server.h
  obex_test_cable.c obex_test_cable.h
)

target_link_libraries ( obex_test
  openobex-apps-common
  openobex
  ${Bluetooth_LIBRARIES}
)

if ( WIN32 )
  target_link_libraries ( obex_test ws2_32 )
endif ( WIN32 )

get_target_property ( obex_test_EXECUTABLE obex_test LOCATION )
install ( PROGRAMS ${obex_test_EXECUTABLE}
  DESTINATION bin
  COMPONENT applications
  OPTIONAL
)

add_dependencies ( openobex-apps obex_test )
