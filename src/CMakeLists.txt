project( TLSF )

include_directories( ${CMAKE_SOURCE_DIR}/3rd )

set( SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/TLSF.cpp
)

set( HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/TLSF.h
)

add_library( TLSF STATIC
    ${SOURCE}
    ${HEADER}
)