
configure_file ( version.h.in version.h @ONLY )

set ( HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/obex.h
  ${CMAKE_CURRENT_SOURCE_DIR}/obex_const.h
  ${CMAKE_CURRENT_BINARY_DIR}/version.h
)
set ( openobex_PUBLIC_HEADERS "${HEADER_FILES}" PARENT_SCOPE )

install ( FILES
  ${HEADER_FILES}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openobex
  COMPONENT devel
)
