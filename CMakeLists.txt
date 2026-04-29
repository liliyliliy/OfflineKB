cmake_minimum_required(VERSION 3.20)
project(OfflineKB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "F:/msys2/mingw64")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)
find_package(SQLite3 REQUIRED)

# 主程序
add_executable(OfflineKB
    main.cpp
    include/mainwindow.h
    src/mainwindow.cpp
    include/databasemanager.h
    src/databasemanager.cpp
    include/document.h
    include/searchengine.h
    src/searchengine.cpp
    include/tokenizer.h
    src/tokenizer.cpp
    resources/dict.qrc
)

target_include_directories(OfflineKB PRIVATE
    include
    third_party/cppjieba
    third_party/cppjieba/include
    third_party/cppjieba/limonp/include
)

target_link_libraries(OfflineKB PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    SQLite3::SQLite3
)

# 测试程序
add_executable(test_search
    tests/test_search.cpp
    include/document.h
    include/searchengine.h
    src/searchengine.cpp
    include/tokenizer.h
    src/tokenizer.cpp
)

target_include_directories(test_search PRIVATE
    include
    third_party/cppjieba
    third_party/cppjieba/include
    third_party/cppjieba/limonp/include
)

target_link_libraries(test_search PRIVATE
    Qt6::Core
)