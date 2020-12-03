include_directories( ${CMAKE_SOURCE_DIR}/src )
include_directories( ${CMAKE_SOURCE_DIR}/3rd )

project( TLSFTest )

set( SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    #${CMAKE_CURRENT_SOURCE_DIR}/GxMemory.cpp
)

add_executable( TLSFTest 
    ${SRC}
)

target_link_libraries( TLSFTest
    TLSF
)

project( Tests )