cmake_minimum_required(VERSION 3.16)
project(inotify)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/Inotify.cpp
    src/Notifier.cpp
)

set(HEADERS
    include/Inotify.hpp
    include/Event.hpp
    include/Notifier.hpp
)

# Create the executable
add_executable(inotify ${SOURCES} ${HEADERS})

# Installation
install(TARGETS inotify DESTINATION /opt/${CMAKE_PROJECT_NAME}/bin)
