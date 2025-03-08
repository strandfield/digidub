cmake_minimum_required(VERSION 3.9)
project(digidub)

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

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

##################################################################
####### apps
##################################################################

add_executable(digidub "src/phash.h" "src/phash.cpp" "src/digidub.cpp")

target_include_directories(digidub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(digidub Qt6::Core Qt6::Gui Qt6::Widgets)
