\# 离线知识库



基于 C++17 开发的全离线智能知识库系统。自研混合检索引擎（倒排索引 + HNSW 向量检索），集成 ONNX Runtime 与 llama.cpp 实现本地语义搜索与轻量化 RAG 问答。



\## 技术栈



\- C++17/20

\- Qt 6

\- SQLite

\- ONNX Runtime + llama.cpp

\- 自研倒排索引 + hnswlib

\- cppjieba



\## 环境准备



\### 1. 安装 vcpkg



\\`\\`\\`powershell

git clone https://github.com/Microsoft/vcpkg.git

cd vcpkg

.\\bootstrap-vcpkg.bat

.\\vcpkg integrate install

\\`\\`\\`



\### 2. 安装依赖



\\`\\`\\`powershell

vcpkg install qtbase:x64-windows sqlite3:x64-windows

\\`\\`\\`



\### 3. 编译项目



\\`\\`\\`powershell

git clone https://github.com/lililyliliy/OfflineKB.git

cd OfflineKB

cmake -B build -S . -DCMAKE\_TOOLCHAIN\_FILE=你的vcpkg路径/scripts/buildsystems/vcpkg.cmake

cmake --build build

\\`\\`\\`



\## 功能



\- 多格式文档解析与智能分块

\- 倒排索引 + BM25 关键词全文检索

\- 文本向量化 + HNSW 语义检索

\- 本地大模型集成的轻量化 RAG 问答

\- 全程离线运行，无网络依赖

