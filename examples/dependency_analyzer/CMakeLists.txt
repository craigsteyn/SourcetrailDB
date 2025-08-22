cmake_minimum_required (VERSION 3.5)

set(EXAMPLE_TARGET_NAME "dependency_analyzer")

set(EXAMPLE_SRC_FILES
	src/main.cpp
)

add_executable(${EXAMPLE_TARGET_NAME} ${EXAMPLE_SRC_FILES})

set_target_properties(${EXAMPLE_TARGET_NAME} PROPERTIES OUTPUT_NAME "dependency_analyzer")

target_include_directories(${EXAMPLE_TARGET_NAME} PUBLIC
	"${CORE_SOURCE_DIR}/include"
	"${CORE_BINARY_DIR}/include"
)

target_link_libraries(${EXAMPLE_TARGET_NAME} ${LIB_CORE_TARGET_NAME} ${CMAKE_DL_LIBS})

if(WIN32)
	# nothing
elseif(APPLE)
	# nothing
elseif(UNIX)
	target_link_libraries(${EXAMPLE_TARGET_NAME} pthread)
endif()
