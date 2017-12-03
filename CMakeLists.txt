cmake_minimum_required (VERSION 3.5)
project(mndb)

set(COMPUTE_VERSION_MAJOR 1)
set(COMPUTE_VERSION_MINOR 0)

#enable_testing()

find_package(Boost REQUIRED COMPONENTS unit_test_framework system filesystem)
find_package(Threads)

include_directories("${PROJECT_SOURCE_DIR}/include")
set(MNDB_SOURCES 
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROJECT_SOURCE_DIR}/src/connection_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/connection.cpp
    ${PROJECT_SOURCE_DIR}/src/nbd.cpp
    )

add_executable(mndb-server ${MNDB_SOURCES})
target_include_directories(mndb-server PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    ${Boost_INCLUDE_DIRS})
target_link_libraries(mndb-server
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    )
