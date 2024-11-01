cmake_minimum_required(VERSION 3.16)
project(inotify)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


find_package(Threads REQUIRED)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/Inotify.cpp
    src/Logger.cpp
    src/FileEvent.cpp
)

set(HEADERS
    src/include/Inotify.hpp
    src/include/Logger.hpp
    src/include/FileEvent.hpp
    src/include/InotifyError.hpp
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_executable(inotify ${SOURCES} ${HEADERS})

target_include_directories(inotify PRIVATE include)

target_link_libraries(inotify PRIVATE Threads::Threads)

install(TARGETS inotify DESTINATION /opt/${CMAKE_PROJECT_NAME}/bin)
