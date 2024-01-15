cmake_minimum_required(VERSION 3.27)
project(multilogtail)

set(CMAKE_CXX_STANDARD 23)

add_executable(multilogtail main.cpp)
target_link_libraries(multilogtail PRIVATE -lncurses -ltinfo)

install(TARGETS multilogtail DESTINATION bin)
