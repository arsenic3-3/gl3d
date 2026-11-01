cmake_minimum_required(VERSION 3.20)
project(gl3d CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(gl3d main.cpp)
target_link_libraries(gl3d PRIVATE glfw glm::glm GLEW::GLEW OpenGL::GL)
if(MSVC)
  target_compile_definitions(gl3d PRIVATE GLEW_STATIC)
  # no console window
  target_link_options(gl3d PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
