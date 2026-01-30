cmake_minimum_required(VERSION 3.9)
project(digidub)

set(DIGIDUB_VERSION_MAJOR 2)
set(DIGIDUB_VERSION_MINOR 1)

add_compile_definitions(-DDIGIDUB_VERSION_MAJOR=${DIGIDUB_VERSION_MAJOR} -DDIGIDUB_VERSION_MINOR=${DIGIDUB_VERSION_MINOR})

##################################################################
####### C++20
##################################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##################################################################
####### Qt
##################################################################

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC TRUE)

find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets REQUIRED)


##################################################################
####### lib
##################################################################

file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.cpp)
file(GLOB_RECURSE HDR_FILES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.h)

add_library(dubbing STATIC ${SRC_FILES} ${HDR_FILES})
target_include_directories(dubbing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib")
target_link_libraries(dubbing Qt6::Core Qt6::Gui)

##################################################################
####### cli app
##################################################################

add_executable(digidub-cli "cli/main.cpp" "digidub.rc")
target_link_libraries(digidub-cli dubbing)

##################################################################
####### editor app
##################################################################

file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp)
file(GLOB_RECURSE HDR_FILES ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.h)
file(GLOB_RECURSE QRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.qrc)

add_executable(matcheditor ${HDR_FILES} ${SRC_FILES} ${QRC_FILES}
  editor/digidub.qrc digidub.rc)

target_include_directories(matcheditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/editor")
target_link_libraries(matcheditor Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
target_link_libraries(matcheditor dubbing)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  set_property(TARGET matcheditor PROPERTY WIN32_EXECUTABLE true)
endif()


##################################################################
####### install
##################################################################

configure_file(Create-Package.ps1.in "${CMAKE_CURRENT_BINARY_DIR}/Create-Package.ps1")
