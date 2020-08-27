if(NOT TARGET uv)
include(ExternalProject)

# Check if libuv is already in sysroot
# FIND_PATH(UV_INCLUDE_DIR NAMES uv.h)
# FIND_LIBRARY(UV_LIBRARIES NAMES uv libuv)
# 
# IF(UV_INCLUDE_DIR AND UV_LIBRARIES)
# 
# 	get_filename_component(UV_INCLUDE_DIR2 ${UV_INCLUDE_DIR} DIRECTORY)
# 	message("LIBUV found: <${UV_LIBRARIES}> <${UV_INCLUDE_DIR}> <${UV_INCLUDE_DIR2}>")
# 	add_library(uv SHARED IMPORTED)
# 
# 	set_target_properties(uv PROPERTIES
# 				  IMPORTED_LOCATION "${UV_LIBRARIES}"
# 				  INTERFACE_INCLUDE_DIRECTORIES "${UV_INCLUDE_DIR}")
# ELSE ()
	# libuv not found in sysroot, build it...
	# message("UV not found in sysroot, build it  <${UV_LIBRARIES}> <${UV_INCLUDE_DIR}>")
	get_filename_component(THIRDPARTY_DIR_TMP ${CMAKE_PARENT_LIST_FILE} PATH)
	get_filename_component(THIRDPARTY_DIR     ${THIRDPARTY_DIR_TMP} PATH)

	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/thirdparty/include)
	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/thirdparty/lib)

	ExternalProject_Add(uv_deps
		GIT_REPOSITORY https://github.com/libuv/libuv.git
		GIT_SHALLOW 1
		CMAKE_ARGS
			-DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/thirdparty/ -DLIBUV_BUILD_TESTS=OFF
		BUILD_IN_SOURCE   1)

	# Set uv library as shared
	add_library(uv SHARED IMPORTED)
	# add_library(uv_static STATIC IMPORTED)

	# add_dependencies(uv_static uv_deps)
	add_dependencies(uv uv_deps)

	set_target_properties(uv PROPERTIES
						  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/thirdparty/lib/libuv.so"
						  INTERFACE_LINK_LIBRARIES "pthread"
						  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/thirdparty/include")

	# set_target_properties(uv_static PROPERTIES
	# set_target_properties(uv_static PR        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/thirdparty/lib/libuv.a"
	# set_target_properties(uv_static PR        INTERFACE_LINK_LIBRARIES "pthread"
	# set_target_properties(uv_static PR        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/thirdparty/include")

	# Set legacy lib variables for compatibility
	SET(UV_INCLUDE_DIR "${CMAKE_BINARY_DIR}/thirdparty/include")
	SET(UV_LIBRARIES "${CMAKE_BINARY_DIR}/thirdparty/lib/libuv.so")
# ENDIF ()
ENDIF(NOT TARGET uv)
