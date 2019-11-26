project(gpendingpool_demo)
cmake_minimum_required(VERSION 2.8)
set (Tutorial_VERSION_MAJOR 1)
set (Tutorial_VERSION_MINOR 0)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
message(${PROJECT_SOURCE_DIR})
message(${PROJECT_BINARY_DIR})
set (VERSION_MAJOR 1)
set (VERSION_MINOR 0)
else()
endif()

add_definitions(-D_DEBUG_LOG_)
add_definitions(-D_NOTICE_LOG_)
add_definitions(-D_WARNING_LOG_)
add_definitions(-D_FATAL_LOG_)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb -std=c++2a -fno-strict-aliasing -Wno-register")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -std=c++2a -fno-strict-aliasing -Wno-register")
set(CMAKE_BUILD_TYPE "Release")

find_package(Boost REQUIRED COMPONENTS log)
if(NOT Boost_FOUND)
    message("Can not find Boost")
endif()

add_definitions(-DBOOST_SPIRIT_THREADSAFE)
add_definitions(-DBOOST_ALL_DYN_LINK)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/common/util/include")
include_directories("${PROJECT_SOURCE_DIR}/")

ADD_LIBRARY(gpendingpool STATIC ${PROJECT_SOURCE_DIR}/src/gpendingpool.cpp)

add_subdirectory(common/util)
add_executable(gpendingpool_demo 
"${PROJECT_SOURCE_DIR}/demo.cpp" 
)
target_link_libraries(gpendingpool_demo common gpendingpool)
