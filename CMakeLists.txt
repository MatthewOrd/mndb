cmake_minimum_required (VERSION 3.5)
project(compute)

set(COMPUTE_VERSION_MAJOR 1)
set(COMPUTE_VERSION_MINOR 0)

#enable_testing()

find_package(Boost REQUIRED COMPONENTS unit_test_framework system)
find_package(Threads)

include_directories("${PROJECT_SOURCE_DIR}/include")
set(COMPUTE_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)

add_executable(compute ${COMPUTE_SOURCES})
target_include_directories(compute PRIVATE 
    "${PROJECT_SOURCE_DIR}/include"
    ${Boost_INCLUDE_DIRS})
target_link_libraries(compute
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    )
