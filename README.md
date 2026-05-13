# OfflineKB 离线知识库系统

OfflineKB 是一个基于 C++17、Qt6、SQLite 和 llama.cpp 的本地离线知识库系统。项目支持 TXT/MD 文档导入、中文关键词检索、BGE 语义向量检索、HNSW 向量索引、chunk 级 RAG 问答，以及本地 GGUF 大模型推理。

系统目标是让普通文档在离线环境下具备“可检索、可追问、可定位来源”的能力，不依赖在线 API。

## 当前功能

- 文档导入：支持 TXT、MD 文档导入，并保存到 SQLite。
- 文档分块：按段落和长度自动切分 chunk，支持 chunk 级检索和问答。
- 关键词检索：基于 cppjieba 分词和 BM25 的中文全文检索。
- 语义检索：支持 BGE 中文 embedding GGUF 模型，使用 hnswlib 建立向量索引。
- 混合召回：结合 BM25、向量检索、题号/关键词打分，提升 RAG 来源准确性。
- 本地 RAG：集成 llama.cpp，使用本地 GGUF 生成模型完成问答。
- 来源引用：AI 回答后显示本轮参考来源。
- 可点击来源：点击来源可跳转到文档检索页，并高亮对应 chunk。
- 文档聚焦：选中文档后，AI 问答优先基于该文档；再次点击同一文档可取消选中。
- 文档名识别：问题中出现文档名时，自动聚焦对应文档。
- 动态回答长度：简单答案、普通问答、总结、分析类问题使用不同生成上限。
- 索引版本校验：保存 `chunks.meta.json`，校验 embedding 模型、向量维度和 chunk 数。
- 模型状态查看：帮助菜单中可查看生成模型、embedding 模型、索引 chunk 数和 GPU/Vulkan 状态。

## 技术栈

- C++17
- Qt 6 Widgets
- SQLite
- cppjieba
- hnswlib
- llama.cpp
- GGUF 生成模型
- GGUF embedding 模型
- Vulkan 后端（可选 GPU 加速）

## 项目结构

```text
OfflineKB/
├── include/                 # 头文件
├── src/                     # 主程序源码
├── tests/                   # 检索和 RAG 评测程序
├── resources/dict/          # cppjieba 词典资源
├── third_party/             # llama.cpp、hnswlib、cppjieba 等依赖
├── models/                  # 可选模型目录
├── CMakeLists.txt
└── README.md
```

## 模型准备

运行 RAG 问答至少需要一个生成模型。若要获得更好的语义检索效果，建议同时准备 embedding 模型。

### 生成模型

推荐使用：

```text
Qwen3-4B-Instruct-2507-Q4_K_M.gguf
```

程序会优先在以下位置查找模型：

```text
build/models/
build/Release/models/
models/
F:/OfflineKB/models/
```

建议放置到：

```text
F:/OfflineKB/build/models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf
```

### Embedding 模型

推荐使用：

```text
bge-small-zh-v1.5-q4_k_m.gguf
```

建议放置到：

```text
F:/OfflineKB/build/models/bge-small-zh-v1.5-q4_k_m.gguf
```

如果没有找到真实 embedding 模型，系统会回退到哈希向量模式，但语义检索和 RAG 来源准确性会明显下降。

## 编译环境

推荐使用 MSYS2 MinGW 64-bit 终端。

### 安装基础依赖

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-sqlite3
```

### Vulkan GPU 加速依赖

项目默认启用 llama.cpp 的 Vulkan 后端。建议安装：

```bash
pacman -S --needed \
  mingw-w64-x86_64-vulkan-headers \
  mingw-w64-x86_64-vulkan-loader \
  mingw-w64-x86_64-spirv-headers \
  mingw-w64-x86_64-spirv-tools
```

同时建议安装 LunarG Vulkan SDK，并确保显卡驱动支持 Vulkan。

如果不需要 GPU 推理，可在 CMake 配置时关闭 Vulkan：

```bash
cmake -B build -S . -G Ninja -DOFFLINEKB_USE_VULKAN=OFF
```

## 编译

在 MSYS2 MinGW 64-bit 终端执行：

```bash
cd /f/OfflineKB
cmake -B build -S . -G Ninja
cmake --build build --parallel
```

如果使用纯 CPU：

```bash
cd /f/OfflineKB
cmake -B build -S . -G Ninja -DOFFLINEKB_USE_VULKAN=OFF
cmake --build build --parallel
```

构建完成后运行：

```bash
cd /f/OfflineKB/build
./OfflineKB.exe
```

## 使用方式

1. 启动 `OfflineKB.exe`。
2. 在“文档检索”页导入 TXT 或 MD 文档。
3. 使用关键词搜索或语义搜索查看文档。
4. 点击某篇文档可预览内容，并将 AI 问答范围聚焦到该文档。
5. 再次点击同一文档可取消聚焦，恢复全库问答。
6. 切换到“AI 问答”页输入问题。
7. 回答生成后，可在“本轮参考来源”中点击来源跳转到原文 chunk。

## RAG 工作流程

```text
导入文档
  ↓
写入 SQLite documents 表
  ↓
切分为 chunks
  ↓
构建 BM25 chunk 索引
  ↓
生成 embedding 向量
  ↓
写入 HNSW 向量索引
  ↓
用户提问
  ↓
文档聚焦 / 文档名识别 / 全库检索
  ↓
BM25 + 向量检索 + 关键词/题号重排
  ↓
拼接 RAG 上下文
  ↓
llama.cpp 本地生成回答
  ↓
显示答案与可点击来源
```

## 索引和数据文件

系统会在应用数据目录中维护数据库与索引文件：

- `offlinekb.db`：SQLite 数据库，保存文档和 chunks。
- `chunks.hnsw`：HNSW 向量索引文件。
- `chunks.meta.json`：索引元信息，包含 embedding 模型名、向量维度、chunk 数和更新时间。

`chunks.meta.json` 示例：

```json
{
  "modelName": "bge-small-zh-v1.5-q4_k_m.gguf",
  "dimension": 512,
  "chunkCount": 42,
  "updatedAt": "2026-05-10T12:00:00"
}
```

启动时如果发现 embedding 模型名、维度或 chunk 数不匹配，系统会自动重建向量索引。

## 模型状态

在菜单中点击：

```text
帮助 -> 当前模型状态
```

可以查看：

- 生成模型名
- Embedding 模型名
- 向量维度
- 索引 chunk 数
- GPU/Vulkan 状态

## 测试程序

项目包含两个辅助测试目标：

```bash
./test_search
```

用于测试关键词检索。

```bash
./eval_qa models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf
```

用于测试本地 RAG 生成行为，检查是否出现复读、截断或无法遵守提示词等问题。

## 常见问题

### 1. 问答来源不准确

优先确认是否已加载真实 BGE embedding 模型。可以在“帮助 -> 当前模型状态”中查看。

如果问题针对某篇文档，建议先在“文档检索”页选中文档，或在问题中明确写出文档名，例如：

```text
总结习题5知识点
```

系统会自动识别 `习题5` 并聚焦对应文档。

### 2. 回答太短或被截断

系统会根据问题类型动态控制生成长度：

- 简单答案类问题使用较短上限。
- 普通问答使用中等上限。
- 总结、知识点、分析类问题使用更高上限。

如果希望更完整，可以在问题中明确写“请详细分析”或“逐项说明依据”。

### 3. 没有 GPU 加速

检查：

- 是否安装 Vulkan SDK。
- 显卡驱动是否支持 Vulkan。
- 是否使用 `-DOFFLINEKB_USE_VULKAN=ON` 配置。
- “帮助 -> 当前模型状态”中 GPU/Vulkan 是否显示启用。

### 4. 找不到模型

确认模型文件名和位置，例如：

```text
F:/OfflineKB/build/models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf
F:/OfflineKB/build/models/bge-small-zh-v1.5-q4_k_m.gguf
```

### 5. 修改 embedding 模型后检索异常

删除旧索引或直接重启程序。系统会通过 `chunks.meta.json` 检查模型名、维度和 chunk 数，不一致时自动重建。

## 后续优化方向

- 增加“重建知识库索引”菜单项。
- 增加“是否参与问答”的文档开关，避免 README、PROMPTS 等项目说明文档污染全库检索。
- 在来源中显示匹配分数、命中关键词和向量相似度。
- 增加“查看本轮 RAG 上下文”功能，方便调试问答质量。
- 支持 PDF、DOCX 等更多文档格式。
