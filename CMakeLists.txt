cmake_minimum_required(VERSION 3.20)
project(OfflineKB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "F:/msys2/mingw64")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Concurrent)
find_package(SQLite3 REQUIRED)

# llama.cpp（仅编译核心库，关闭测试/示例/服务器）
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

# ----------------------------------------------------------------------------
# GGML 后端选项 —— 必须放在 add_subdirectory(llama.cpp) 之前才能生效
#
# 推荐 Vulkan：MinGW 完全兼容，无需 CUDA Toolkit；性能 ≈ CUDA 的 90-95%。
# 使用前：安装 LunarG Vulkan SDK（https://vulkan.lunarg.com/sdk/home#windows）
# 并确保系统能在命令行找到 vulkaninfo / VK_SDK_PATH 环境变量已设置。
#
# 如果只想 CPU 推理，把 GGML_VULKAN 改为 OFF；CPU 加速指令仍会生效。
# ----------------------------------------------------------------------------
# 切换 Vulkan / 纯 CPU：cmake 配置时加 -DOFFLINEKB_USE_VULKAN=OFF 即可关掉
option(OFFLINEKB_USE_VULKAN "Build llama.cpp with Vulkan backend" ON)

if (OFFLINEKB_USE_VULKAN)
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    message(STATUS "OfflineKB: Vulkan backend ENABLED")
else()
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
    message(STATUS "OfflineKB: Vulkan backend DISABLED (CPU only)")
endif()
set(GGML_NATIVE  ON  CACHE BOOL "" FORCE)  # 自动启用本机支持的 SIMD
set(GGML_AVX2    ON  CACHE BOOL "" FORCE)
set(GGML_F16C    ON  CACHE BOOL "" FORCE)
set(GGML_FMA     ON  CACHE BOOL "" FORCE)
# 减少 llama.cpp 自身的可执行目标，加速编译
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON   ON  CACHE BOOL "" FORCE)

add_subdirectory(third_party/llama.cpp)

set(OFFLINEKB_CORE_SOURCES
    include/databasemanager.h
    src/databasemanager.cpp
    include/document.h
    include/kbservice.h
    src/kbservice.cpp
    include/searchengine.h
    src/searchengine.cpp
    include/tokenizer.h
    src/tokenizer.cpp
    include/vectorindex.h
    src/vectorindex.cpp
    include/embeddingengine.h
    src/embeddingengine.cpp
    include/ragengine.h
    src/ragengine.cpp
    include/rag_policy.h
    src/rag_policy.cpp
)

set(OFFLINEKB_CORE_INCLUDE_DIRS
    include
    third_party/hnswlib
    third_party/cppjieba
    third_party/cppjieba/include
    third_party/cppjieba/limonp/include
    third_party/llama.cpp/include
)

# 主程序
add_executable(OfflineKB
    main.cpp
    include/mainwindow.h
    src/mainwindow.cpp
    include/chatwidget.h
    src/chatwidget.cpp
    resources/dict.qrc
    ${OFFLINEKB_CORE_SOURCES}
)

target_include_directories(OfflineKB PRIVATE ${OFFLINEKB_CORE_INCLUDE_DIRS})

target_link_libraries(OfflineKB PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    SQLite3::SQLite3
    llama
)

# headless CLI for MCP integration
add_executable(offlinekb-cli
    tools/offlinekb_cli.cpp
    ${OFFLINEKB_CORE_SOURCES}
)

target_include_directories(offlinekb-cli PRIVATE ${OFFLINEKB_CORE_INCLUDE_DIRS})

target_link_libraries(offlinekb-cli PRIVATE
    Qt6::Core
    SQLite3::SQLite3
    llama
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

# ----------------------------------------------------------------------------
# 评测集：固定问答用例，验证 RagEngine 不会回退到复读原文 / 截断 prompt 等问题。
# 用法：
#   ./eval_qa  <gguf 模型路径>
# 例如：
#   ./eval_qa  models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf
# ----------------------------------------------------------------------------
add_executable(eval_qa
    tests/eval_qa.cpp
    include/ragengine.h
    src/ragengine.cpp
)

target_include_directories(eval_qa PRIVATE
    include
    third_party/llama.cpp/include
)

target_link_libraries(eval_qa PRIVATE
    llama
)

add_executable(eval_retrieval
    tests/eval_retrieval.cpp
    ${OFFLINEKB_CORE_SOURCES}
)

target_include_directories(eval_retrieval PRIVATE ${OFFLINEKB_CORE_INCLUDE_DIRS})

target_link_libraries(eval_retrieval PRIVATE
    Qt6::Core
    SQLite3::SQLite3
    llama
)

enable_testing()
add_test(NAME test_search COMMAND test_search)
add_test(NAME eval_retrieval COMMAND eval_retrieval)