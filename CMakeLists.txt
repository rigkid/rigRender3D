file(READ "${CMAKE_CURRENT_LIST_DIR}/pack.json" PACK_JSON)
string(JSON PACK_NAME GET "${PACK_JSON}" name)

if(NOT TARGET rigComponent)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigComponent_dir "${RIGKIT_ROOT}/packs/rigComponent")
	else()
		set(_rigComponent_dir "${CMAKE_SOURCE_DIR}/packs/rigComponent")
	endif()
	if(EXISTS "${_rigComponent_dir}/CMakeLists.txt")
		add_subdirectory("${_rigComponent_dir}" "${CMAKE_BINARY_DIR}/packs/rigComponent")
	else()
		message(FATAL_ERROR "rigRender3D requires rigComponent under packs/rigComponent")
	endif()
endif()

if(NOT TARGET rigSystems)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigSystems_dir "${RIGKIT_ROOT}/packs/rigSystems")
	else()
		set(_rigSystems_dir "${CMAKE_SOURCE_DIR}/packs/rigSystems")
	endif()
	if(EXISTS "${_rigSystems_dir}/CMakeLists.txt")
		add_subdirectory("${_rigSystems_dir}" "${CMAKE_BINARY_DIR}/packs/rigSystems")
	else()
		message(FATAL_ERROR "rigRender3D requires rigSystems under packs/rigSystems")
	endif()
endif()

if(NOT TARGET rigImage)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigImage_dir "${RIGKIT_ROOT}/packs/rigImage")
	else()
		set(_rigImage_dir "${CMAKE_SOURCE_DIR}/packs/rigImage")
	endif()
	if(EXISTS "${_rigImage_dir}/CMakeLists.txt")
		add_subdirectory("${_rigImage_dir}" "${CMAKE_BINARY_DIR}/packs/rigImage")
	else()
		message(FATAL_ERROR "rigRender3D requires rigImage under packs/rigImage")
	endif()
endif()

add_library(${PACK_NAME} STATIC
	src/rigRender3D.cpp
	src/rigRender3D.h
	src/SMeshPresent3D.cpp
	src/SMeshPresent3D.h
	src/SurfacePass.cpp
	src/SurfacePass.h
	src/SurfaceIbl.h
	src/shaders/BlitShader.h
	src/shaders/MeshShader.h
	src/shaders/SurfaceShader.h
)
target_link_libraries(${PACK_NAME} PUBLIC rigkit rigComponent rigSystems rigImage)
target_include_directories(${PACK_NAME} PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
)
message(STATUS "Configured pack: ${PACK_NAME} (3D mesh present)")
