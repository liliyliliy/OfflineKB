# 离线知识库

基于 C++17 开发的全离线智能知识库系统。自研混合检索引擎（倒排索引 + HNSW 向量检索），集成 ONNX Runtime 与 llama.cpp 实现本地语义搜索与轻量化 RAG 问答。

## 技术栈

- C++17/20
- Qt 6
- SQLite
- ONNX Runtime + llama.cpp
- 自研倒排索引 + hnswlib
- cppjieba

## 环境准备

### 1. 安装 MSYS2
下载并安装到 `F:\msys2`：
https://github.com/msys2/msys2-installer/releases/download/2025-06-22/msys2-x86_64-20250622.exe

### 2. 配置清华镜像（国内用户）
打开 MSYS2 MinGW 64-bit 终端，执行：
echo "Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw64/" > /etc/pacman.d/mirrorlist.mingw64
echo "Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw32/" > /etc/pacman.d/mirrorlist.mingw32
echo "Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/\$arch/" > /etc/pacman.d/mirrorlist.msys

### 3. 安装依赖
pacman -Syu
pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-sqlite3

### 4. 配置环境变量
环境变量 PATH 添加：
- `F:\msys2\mingw64\bin`
- `F:\msys2\usr\bin`

### 5. 编译项目
git clone https://github.com/lililyliliy/OfflineKB.git
cd OfflineKB
cmake -B build -S . -G "MinGW Makefiles"
cmake --build build

### 6. 下载模型
项目使用 Qwen3-4B-Instruct（Q4_K_M 量化，约 2.5GB），通过 ModelScope 下载：
git clone https://www.modelscope.cn/Qwen/Qwen3-4B-Instruct-GGUF.git
cp Qwen3-4B-Instruct-GGUF/*.gguf models/

### 7. 安装 Vulkan SDK（可选，GPU 加速）
从 https://vulkan.lunarg.com/sdk/home 下载并安装。编译时启用 `-DGGML_VULKAN=ON` 可获得 GPU 加速。

若编译失败，系统将自动回退为纯 CPU 推理，使用 `-DGGML_NATIVE=ON` 优化后速度也可接受。

## 数据库设计

### documents 表
| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PRIMARY KEY AUTOINCREMENT | 文档唯一ID |
| title | TEXT | 文档标题（文件名） |
| content | TEXT | 文档正文内容 |
| file_path | TEXT | 原始文件路径 |
| created_at | TEXT | 导入时间 |

### 倒排索引结构（内存）
| 结构 | 类型 | 说明 |
|------|------|------|
| inverted_index_ | unordered_map<string, vector<pair<int, int>>> | 词 → {文档ID, 词频} |
| doc_lengths_ | unordered_map<int, int> | 文档ID → 总词数 |
| avg_doc_length_ | double | 平均文档长度（BM25用） |

## 功能

- 多格式文档解析与智能分块
- 倒排索引 + BM25 关键词全文检索
- 文本向量化 + HNSW 语义检索
- 本地大模型集成的轻量化 RAG 问答
- 全程离线运行，无网络依赖
