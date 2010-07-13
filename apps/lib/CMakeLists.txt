
#
# a static library to not compile those multiple times
#
add_library ( openobex-apps-common STATIC EXCLUDE_FROM_ALL
  obex_io.c obex_io.h
  obex_put_common.c obex_put_common.h
)

target_link_libraries ( openobex-apps-common openobex )
