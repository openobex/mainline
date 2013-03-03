
add_custom_target ( doc )

set ( SOURCES
  "${CMAKE_SOURCE_DIR}/lib/api.c"
  "${CMAKE_SOURCE_DIR}/include/openobex/obex.h"
  "${CMAKE_SOURCE_DIR}/include/openobex/obex_const.h"
  "${CMAKE_SOURCE_DIR}/doc/openobex.coverpage"
)

find_package( Doxygen )
if ( DOXYGEN_FOUND )
  set ( DOC_OUTPUT_TYPES
    HTML
    LATEX
  )
  set ( DOC_HTML_OUTPUT_FILE "index.html" )
  set ( DOC_LATEX_OUTPUT_FILE "refman.tex" )

  foreach ( type ${DOC_OUTPUT_TYPES} )
    foreach ( i ${DOC_OUTPUT_TYPES} )
      set ( DOC_${i}_OUTPUT "NO" )
    endforeach ( i )
    set ( DOC_${type}_OUTPUT "YES" )
    foreach ( src ${SOURCES} )
      set ( DOC_SOURCE_FILES "${DOC_SOURCE_FILES} ${src}" )
    endforeach ( src )
    configure_file (
      ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
      ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_${type}
      @ONLY
    )
    if ( DOC_${type}_OUTPUT_FILE )
      string ( TOLOWER "${type}" type_low )
      set ( DOC_${type}_OUTPUT_FILE_ABS "${CMAKE_CURRENT_BINARY_DIR}/${type_low}/${DOC_${type}_OUTPUT_FILE}" )

      add_custom_command ( OUTPUT "${DOC_${type}_OUTPUT_FILE_ABS}"
          COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_${type}"
          DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_${type}" ${SOURCES}
          VERBATIM
      )

      add_custom_target ( apidoc_${type_low} DEPENDS ${DOC_${type}_OUTPUT_FILE_ABS} )
    endif ( DOC_${type}_OUTPUT_FILE )
  endforeach ( type )

  set_directory_properties ( PROPERTIES
    ADDITIONAL_MAKE_CLEAN_FILES "html;latex"
  )
  add_dependencies ( openobex apidoc_html )
  add_dependencies ( doc apidoc_html )

  install ( DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/html"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT documentation
  )
endif ( DOXYGEN_FOUND )

find_package ( Docbook )
if ( Docbook_FOUND )
  include ( ${Docbook_USE_FILE} )
  if ( UNIX )
    option (BUILD_APPS_HELP_MAN "Build manpage application help files" ON)
    option (BUILD_APPS_HELP_HTML "Build HTML application help files" OFF)
  else ( UNIX )
    option (BUILD_APPS_HELP_HTML "Build HTML application help files" ON)
  endif ( UNIX )

  configure_file ( referenceinfo.xml.in referenceinfo.xml @ONLY )
  set ( OPENOBEX_APP_XML_FILE "${CMAKE_CURRENT_SOURCE_DIR}/openobex-apps.xml" )
  list ( APPEND XMLLINT_OPTIONS
    --path "${CMAKE_CURRENT_BINARY_DIR}"
  )
  list ( APPEND XSLT_XSLTPROC_OPTIONS
    --path "${CMAKE_CURRENT_BINARY_DIR}"
  )
  if ( BUILD_APPS_HELP_MAN )
    docbook_generate ( manpage "${OPENOBEX_APP_XML_FILE}" XSLT_FILES_MAN )
    foreach ( file ${XSLT_FILES_MAN} )
      list ( APPEND OPENOBEX_HELP_FILES ${file} )
      get_filename_component ( fileExt ${file} EXT )
      string ( REGEX REPLACE "^[.]" "" fileExt ${fileExt} )
      install (
	FILES ${CMAKE_CURRENT_BINARY_DIR}/${file}
	DESTINATION ${CMAKE_INSTALL_MANDIR}/man${fileExt}
	PERMISSIONS OWNER_READ OWNER_WRITE
                    GROUP_READ
                    WORLD_READ
	COMPONENT documentation
	OPTIONAL
      )
    endforeach ( file )
  endif ( BUILD_APPS_HELP_MAN )

  if ( BUILD_APPS_HELP_HTML )
    docbook_generate ( html "${OPENOBEX_APP_XML_FILE}" XSLT_FILES_HTML )
    foreach ( file ${XSLT_FILES_HTML} )
      list ( APPEND OPENOBEX_HELP_FILES ${file} )
      install (
	FILES ${CMAKE_CURRENT_BINARY_DIR}/${file}
	DESTINATION ${CMAKE_INSTALL_DOCDIR}
	PERMISSIONS OWNER_READ OWNER_WRITE
                    GROUP_READ
                    WORLD_READ
        COMPONENT documentation
        OPTIONAL
      )
    endforeach ( file )
  endif ( BUILD_APPS_HELP_HTML )

  if ( OPENOBEX_HELP_FILES )
    add_custom_target ( openobex-apps-doc DEPENDS ${OPENOBEX_HELP_FILES} )
    add_dependencies ( openobex-apps openobex-apps-doc )
    add_dependencies ( doc openobex-apps-doc )
  endif ( OPENOBEX_HELP_FILES )
endif ( Docbook_FOUND )
