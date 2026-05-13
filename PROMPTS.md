# OfflineKB AI 开发 Prompts

## 目录

1. 项目骨架生成
2. 全文检索引擎
3. 分词和中英文混合搜索修复
4. RAG 问答接入
5. 真实 Embedding 模型接入
6. 索引持久化与版本校验
7. 来源引用和可点击跳转
8. RAG 质量调优

---

## 1. 项目骨架生成

### 目标

生成一个完整可编译、可运行的 Qt6 桌面应用骨架，包含主窗口、SQLite 数据库、文档导入和基础预览。

### Prompt

```text
你是一名资深 C++ 架构师。请按照以下要求，为我生成一个完整可编译、可直接运行的“离线知识库系统”桌面应用第一阶段骨架项目。

项目根目录：
F:/OfflineKB

技术约束：
- 语言：C++17
- UI 框架：Qt6 Widgets
- 构建系统：CMake
- 数据库：SQLite
- 编译器：GCC / MinGW-w64
- MSYS2 / Qt6 路径：F:/msys2/mingw64
- 运行系统：Windows

必须实现功能：

1. 项目结构
- CMakeLists.txt
- main.cpp
- include/
- src/
- resources/
- tests/
- 项目必须可编译、可运行，并显示主窗口。

2. 主窗口
- 标题：“离线知识库系统”
- 顶部搜索栏：QLineEdit + 搜索按钮
- 左侧文档列表：QListWidget
- 右侧文档预览：QTextEdit，只读
- 底部状态栏：显示文档数量
- 菜单栏：文件（导入文档、退出）、帮助（关于）
- 支持文件拖拽导入
- 全局支持中文，不允许乱码

3. 数据库模块
- 自动初始化 SQLite 数据库文件
- 自动创建 documents 表
- documents 表字段：
  - id INTEGER PRIMARY KEY AUTOINCREMENT
  - title TEXT
  - content TEXT
  - file_path TEXT
  - created_at TEXT
- 提供文档插入、查询接口
- 数据库操作必须有错误处理

4. 文档导入
- 支持 TXT / MD 格式
- 支持菜单导入和拖拽导入
- 读取内容后写入数据库
- 导入后自动刷新左侧列表

5. 交互逻辑
- 点击左侧文档后，右侧显示正文内容
- 状态栏实时显示总文档数
- 界面布局合理、可缩放

CMake 必须配置：
- cmake_minimum_required(VERSION 3.20)
- project(OfflineKB)
- set(CMAKE_CXX_STANDARD 17)
- set(CMAKE_PREFIX_PATH "F:/msys2/mingw64")
- find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
- find_package(SQLite3 REQUIRED)

代码风格：
- 头文件使用 .h，实现文件使用 .cpp
- 类名 PascalCase，函数名 camelCase
- 关键函数添加中文注释
- main.cpp 只保留启动逻辑
- 代码干净、可直接编译运行

输出要求：
请生成完整的以下文件，每个文件用文件名标注，代码放在代码块中：
1. CMakeLists.txt
2. main.cpp
3. include/mainwindow.h
4. src/mainwindow.cpp
5. include/databasemanager.h
6. src/databasemanager.cpp
7. include/document.h

确保代码无报错，可直接编译运行。
```

---

## 2. 全文检索引擎

### 目标

在已有骨架基础上加入 cppjieba 分词和 BM25 关键词检索。

### Prompt

```text
你是一名资深 C++ 架构师。请为“离线知识库系统”实现第二阶段：全文检索引擎。

现有项目路径：
F:/OfflineKB

技术约束：
- C++17
- Qt6 Widgets / Qt6 Sql
- MinGW-w64，路径 F:/msys2/mingw64
- 新增依赖：cppjieba
- 代码必须线程安全
- 所有注释使用中文
- 支持中文搜索和停用词过滤
- 不要重写无关文件

需要新增文件：

1. include/searchengine.h
- 类名：SearchEngine
- 方法：
  - void buildIndex(const std::vector<Document>& docs)
  - std::vector<std::pair<int, double>> search(const std::string& query)
  - void addDocument(const Document& doc)
  - void removeDocument(int docId)
- 内部数据结构：
  - unordered_map<string, vector<pair<int, int>>> invertedIndex_
  - unordered_map<int, int> docLengths_
  - double avgDocLength_
- 使用互斥锁保证线程安全。

2. include/tokenizer.h
- 类名：Tokenizer
- 封装 cppjieba
- 方法：
  - std::vector<std::string> tokenize(const std::string& text)
- 支持停用词过滤。

3. src/searchengine.cpp
- 实现 SearchEngine 所有方法
- buildIndex：遍历文档，分词后构建倒排索引
- search：查询分词 -> 查倒排索引 -> 计算 BM25 分数 -> 按分数降序返回
- BM25 参数：k1 = 1.5，b = 0.75
- 完整错误处理

4. src/tokenizer.cpp
- 初始化 cppjieba 词典路径
- 实现分词和停用词过滤

5. tests/test_search.cpp
- 添加简单测试用例
- 插入测试文档，搜索关键词，验证结果非空且排序合理

需要修改现有文件：

1. include/mainwindow.h
- 添加 SearchEngine、Tokenizer 成员
- 添加 onSearch() 槽函数
- 添加 refreshSearchResults(...) 方法

2. src/mainwindow.cpp
- 初始化 Tokenizer 和 SearchEngine
- 搜索按钮连接到 onSearch
- 文档导入后更新检索索引
- 搜索结果显示到左侧列表

3. CMakeLists.txt
- 添加 cppjieba include 路径
- 添加新增 cpp 文件
- 添加 test_search 目标

输出要求：
- 新文件输出完整内容
- 现有文件只输出修改部分
- 每个文件用文件名作为标题
- 代码必须可编译运行
```

---

## 3. 分词和中英文混合搜索修复

### 目标

修复英文单词、数字、CMakeLists 等中英文混合内容无法稳定检索的问题。

### Prompt

```text
请帮我修复“离线知识库系统”的 tokenizer.cpp 和 searchengine.cpp，解决以下问题：

问题：
1. 当前分词器对英文单词和数字处理不稳定，例如 CMakeLists、Qt6、SQLite、BM25 可能搜索不到。
2. 中英文混合文本需要同时支持中文分词和英文关键词检索。
3. buildIndex 和 search 必须统一大小写，避免大小写导致查不到。

具体要求：

tokenizer.cpp：
- 在 tokenize 函数中，先用 QRegularExpression 提取英文单词和数字。
- 英文/数字正则建议使用：\b[A-Za-z0-9_\.\-]+\b
- 英文单词统一转小写后加入结果。
- 中文部分继续使用 cppjieba 分词。
- 合并中英文 token 后再做停用词过滤。
- 避免重复 token 过多影响 BM25。

searchengine.cpp：
- buildIndex 时所有 token 统一转小写。
- search 时查询 token 也统一转小写。
- 保持 BM25 逻辑不变。
- 不要破坏现有中文搜索。

输出要求：
- 只输出需要修改的代码片段。
- 中文注释。
- 不要输出整个文件。
```

---

## 4. RAG 问答接入

### 目标

接入 llama.cpp 本地 GGUF 模型，实现基于检索上下文的离线 RAG 问答。

### Prompt

```text
请为“离线知识库系统”接入本地 RAG 问答功能。

当前项目：
- C++17
- Qt6 Widgets
- SQLite
- cppjieba + BM25
- 项目路径：F:/OfflineKB

目标：
使用 llama.cpp 加载本地 GGUF 模型，在 AI 问答页中基于检索到的文档内容回答问题。

要求：

1. 新增 RagEngine
- 文件：
  - include/ragengine.h
  - src/ragengine.cpp
- 使用 llama.cpp C API
- 支持加载 GGUF 模型
- 支持 ask(question, context)
- 每次 ask 前必须清空 KV cache
- 使用 GGUF 自带 chat template，不要手写伪 chat 标签
- 支持 n_ctx、n_batch、n_gpu_layers、n_predict 等参数配置
- 支持 stop() 中断
- 返回完整回答字符串

2. MainWindow 集成
- 新增 AI 问答 Tab
- 用户输入问题后：
  - 先检索相关文档或 chunk
  - 拼接 RAG 上下文
  - 在 worker 线程中调用 RagEngine::ask
  - UI 线程显示回答
- 推理过程中不要阻塞界面

3. Prompt 约束
- 模型只能基于【文档】内容回答
- 文档没有相关内容时回答“无法回答”
- 简单问题简洁回答
- 总结/分析类问题允许展开

4. 性能指标
- 记录 prompt token 数
- 记录生成 token 数
- 记录 prefill 时间
- 记录 decode 时间
- 记录 tokens/s
- 在状态栏显示简要指标

输出要求：
- 给出需要新增和修改的文件。
- 不要重写无关模块。
- 代码必须可编译。
- 说明模型文件推荐放置路径。
```

---

## 5. 真实 Embedding 模型接入

### 目标

用 BGE 中文 embedding GGUF 替代哈希向量，提高语义检索准确性。

### Prompt

```text
请为“离线知识库系统”接入真实中文 embedding 模型。

背景：
当前项目已有 EmbeddingEngine，但语义向量质量不足。请使用 llama.cpp 加载 BGE 中文 GGUF embedding 模型。

推荐模型：
bge-small-zh-v1.5-q4_k_m.gguf

推荐路径：
F:/OfflineKB/build/models/bge-small-zh-v1.5-q4_k_m.gguf

要求：

1. EmbeddingEngine 改造
- 支持 initEmbeddingModel(modelPath, nGpuLayers)
- 使用 llama.cpp C API 加载 embedding GGUF
- context 参数必须启用 embeddings
- pooling 使用 mean pooling
- 动态读取模型输出维度
- dimension() 返回真实维度
- 加载失败时回退到原哈希向量
- 提供 modelName()，真实模型返回文件名，回退时返回 hash-embedding

2. encode(text)
- 对真实 embedding 模型使用 llama_encode
- 正确设置 batch.logits，确保 token 被标记为输出
- 输出向量必须 L2 归一化
- 空文本返回零向量或安全回退

3. MainWindow 集成
- 启动时优先查找 BGE 模型
- 成功加载后状态栏显示 BGE 真模型和维度
- 失败时状态栏显示哈希回退
- 不要用启动弹窗打扰用户

输出要求：
- 修改 include/embeddingengine.h
- 修改 src/embeddingengine.cpp
- 修改 src/mainwindow.cpp 中模型初始化逻辑
- 保持没有 embedding 模型时程序仍可运行
```

---

## 6. 索引持久化与版本校验

### 目标

保存 HNSW 索引，并通过 meta 文件判断索引是否与当前 embedding 模型匹配。

### Prompt

```text
请为“离线知识库系统”实现索引版本校验。

当前问题：
VectorIndex 只按 chunk 数量判断 chunks.hnsw 是否可用。请改为保存 chunks.meta.json，并在加载时校验。

要求：

1. chunks.meta.json
保存字段：
- modelName：embedding 模型名
- dimension：向量维度
- chunkCount：chunk 数量
- updatedAt：更新时间

示例：
{
  "modelName": "bge-small-zh-v1.5-q4_k_m.gguf",
  "dimension": 512,
  "chunkCount": 42,
  "updatedAt": "2026-05-10T12:00:00"
}

2. 加载逻辑
- 如果 chunks.meta.json 不存在，重建索引。
- 如果 modelName 不一致，重建索引。
- 如果 dimension 不一致，重建索引。
- 如果 chunkCount 不一致，重建索引。
- 如果 meta 校验通过，再加载 chunks.hnsw。

3. 保存逻辑
- 保存 chunks.hnsw 后，同时写入 chunks.meta.json。
- 保存失败不影响本次运行，但不要让程序崩溃。

需要修改：
- include/embeddingengine.h：确保有 modelName()
- src/embeddingengine.cpp：记录模型文件名
- include/mainwindow.h：新增 vectorIndexMetaPath()
- src/mainwindow.cpp：改造 rebuildChunkIndexes()

输出要求：
- 只修改相关文件
- 中文注释
- 不要修改计划文档
```

---

## 7. 来源引用和可点击跳转

### 目标

让 RAG 回答后的来源可点击，并跳转到文档检索页高亮对应 chunk。

### Prompt

```text
请为“离线知识库系统”实现可点击来源功能。

当前问题：
RAG 回答后显示的 [来源: xxx] 是纯文本，无法跳转。

目标：
回答后在“本轮参考来源”区域显示可点击链接。点击来源后：
- 自动切换到“文档检索”Tab
- 选中对应文档
- 在右侧预览区定位并高亮对应 chunk

要求：

1. 数据结构
在 MainWindow 中新增 RagSourceEntry：
- int documentId
- int chunkId
- int chunkIndex
- QString title
- QString chunkContent

RagContextBundle 新增：
- QVector<RagSourceEntry> entries

2. buildRagContext
- 每加入一个 chunk 到上下文，同时 push 一个 RagSourceEntry。

3. ChatWidget
- 来源区域使用 QTextBrowser 或支持 anchorClicked 的控件。
- setSources 接收 HTML。
- 链接格式使用 source:N。
- 点击后 emit sourceClicked(int index)。

4. MainWindow
- connect ChatWidget::sourceClicked 到 onSourceLinkClicked(int)
- onSourceLinkClicked 中：
  - 根据 index 找到 RagSourceEntry
  - 切到文档检索 Tab
  - 选中对应文档
  - 加载文档内容
  - 用 QTextDocument::find 定位 chunk 片段
  - 用 QTextEdit::ExtraSelection 高亮

5. 兼容要求
- 如果精确 chunk 找不到，取前 80 / 40 / 20 字逐步兜底。
- 点击来源不应触发“再次点击取消选中”的逻辑。

输出要求：
- 修改 mainwindow.h / mainwindow.cpp
- 修改 chatwidget.h / chatwidget.cpp
- 不要改无关文件
```

---

## 8. RAG 质量调优

### 目标

让 RAG 能根据问题类型动态选择范围、来源和回答长度。

### Prompt

```text
请优化“离线知识库系统”的 RAG 问答质量。

当前问题：
1. 全库检索时来源可能不准确。
2. 用户选中文档后，希望 AI 问答优先基于该文档。
3. 用户问题里出现文档名时，希望自动聚焦该文档。
4. 简单问题不应长篇解释，分析类问题不能被截断。
5. “分别总结所有文档”时，需要覆盖每篇文档，每篇只输出一段总结。

要求：

1. 文档聚焦
- 点击文档后，AI 问答范围设置为该文档。
- 再次点击同一文档，取消选中，恢复全库模式。
- 状态栏提示当前范围。

2. 文档名识别
- 如果问题中出现文档标题或文件名，自动聚焦该文档。
- 支持“习题5”“习题 5”“习题五”等归一化匹配。
- 问题指定文档优先级高于当前选中文档。

3. 多文档总结
- 如果问题包含“分别”“每篇”“所有文档”“全部文档”等意图，进入多文档覆盖模式。
- 每篇文档合并为一个上下文 block。
- 每篇文档在来源面板中只显示一条来源。
- 模型回答时每篇文档只输出一段总结。
- 总结要覆盖该文档主要内容。

4. 来源排序
- 普通全库检索使用 BM25 + 向量召回。
- 降低向量召回权重，避免泛语义污染。
- 增加关键词、题号、数字、题型强匹配打分。
- “第 3 题”“第三道”“选择题答案”等问题要优先命中对应 chunk。

5. 动态回答长度
- 简单答案类：短回答。
- 普通问答：中等长度。
- 总结类：允许条目式展开。
- 分析/解析/原因/依据类：提高生成上限，必须逐项分析，不要中途省略。

输出要求：
- 修改 RagEngine 的问题类型判断和生成上限。
- 修改 MainWindow 的 buildRagContext。
- 保留现有可点击来源功能。
- 给出编译和测试建议。
```