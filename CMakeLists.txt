cmake_minimum_required(VERSION 3.24)

project(SmartDashboardWorkspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(SmartDashboard_Interface_direct)
add_subdirectory(ClientInterface_direct)
add_subdirectory(SmartDashboard)
