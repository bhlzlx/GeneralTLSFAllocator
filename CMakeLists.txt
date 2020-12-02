cmake_minimum_required (VERSION 3.4)

project( TLSF-Allocator )

set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmakeModules )

include( basicEnv )
include( outputPath )

add_subdirectory( src )
add_subdirectory( test )