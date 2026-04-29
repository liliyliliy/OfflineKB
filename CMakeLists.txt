cmake_minimum_required(VERSION 3.20)
project(OfflineKB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "F:/msys2/mingw64")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(SQLite3 REQUIRED)

add_executable(OfflineKB
    main.cpp
    include/mainwindow.h
    src/mainwindow.cpp
    include/databasemanager.h
    src/databasemanager.cpp
    include/document.h
)

target_include_directories(OfflineKB PRIVATE include)

target_link_libraries(OfflineKB PRIVATE Qt6::Widgets Qt6::Sql)

if(TARGET SQLite::SQLite3)
    target_link_libraries(OfflineKB PRIVATE SQLite::SQLite3)
elseif(TARGET SQLite3::SQLite3)
    target_link_libraries(OfflineKB PRIVATE SQLite3::SQLite3)
elseif(DEFINED SQLite3_LIBRARIES)
    target_include_directories(OfflineKB PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(OfflineKB PRIVATE ${SQLite3_LIBRARIES})
else()
    message(FATAL_ERROR "Unable to link SQLite3 target or libraries.")
endif()