get_filename_component(SELF_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH) # ...../lib/cmake/OpenObex-x.y/
include(${SELF_DIR}/openobex-targets.cmake)

get_filename_component(INSTALL_DIR "${SELF_DIR}" PATH)    # ...../lib/cmake/
get_filename_component(INSTALL_DIR "${INSTALL_DIR}" PATH) # ...../lib/
get_filename_component(INSTALL_DIR "${INSTALL_DIR}" PATH) # .....
set(OpenObex_INCLUDE_DIRS "${INSTALL_DIR}/include/openobex")
