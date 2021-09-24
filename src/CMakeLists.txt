cmake_minimum_required(VERSION 3.2)
project(cp-editor)

add_subdirectory(division)
set(SOURCE_FILES main.cpp)

add_executable(cp-editor ${SOURCE_FILES})
target_link_libraries(cp-editor division)
install(TARGETS cp-editor DESTINATION ${DIVISIBLE_INSTALL_BIN_DIR})
