cmake_minimum_required (VERSION 3.5)

set(EXAMPLE_TARGET_NAME "code_chunker")

set(EXAMPLE_SRC_FILES
    src/main.cpp
    yyjson.c
)

add_executable(${EXAMPLE_TARGET_NAME} ${EXAMPLE_SRC_FILES})

set_target_properties(${EXAMPLE_TARGET_NAME} PROPERTIES OUTPUT_NAME "code_chunker")

# Require C++17 for std::filesystem
target_compile_features(${EXAMPLE_TARGET_NAME} PRIVATE cxx_std_17)

target_include_directories(${EXAMPLE_TARGET_NAME} PUBLIC
    "${CORE_SOURCE_DIR}/include"
    "${CORE_BINARY_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${EXAMPLE_TARGET_NAME} ${LIB_CORE_TARGET_NAME} ${CMAKE_DL_LIBS})

if(WIN32)
    # nothing
elseif(APPLE)
    # nothing
elseif(UNIX)
    target_link_libraries(${EXAMPLE_TARGET_NAME} pthread)
endif()
