\# AI 开发 Prompts 文档



\## 第一阶段：项目骨架

你是一名资深 C++ 架构师，请按照以下要求，为我生成一个完整可编译、可直接运行的 "离线知识库系统" 桌面应用第一阶段骨架项目。



=== 技术约束 ===

\- 语言：C++17

\- UI框架：Qt6 (Widgets)

\- 构建系统：CMake

\- 数据库：SQLite

\- 编译器：GCC (MinGW-w64, 路径 F:/msys2/mingw64)

\- Qt6 路径：F:/msys2/mingw64

\- 运行系统：Windows



=== 必须实现功能 ===

1\. 项目结构

\- CMakeLists.txt，用 find\_package 找 Qt6 Widgets Sql 和 SQLite3

\- 目录结构：src/ include/ resources/ tests/

\- 可编译、可运行，显示主窗口



2\. 主窗口

\- 标题："离线知识库系统"

\- 顶部搜索栏（QLineEdit + 搜索按钮）

\- 左侧文档列表（QListWidget）

\- 右侧文档内容预览（QTextEdit，只读）

\- 底部状态栏，显示文档数量

\- 菜单栏：文件（导入文档、退出）、帮助（关于）

\- 支持文件拖拽导入

\- 全局支持中文不乱码



3\. 数据库模块

\- 自动初始化 SQLite 数据库文件

\- 自动创建 documents 表

字段：

id INTEGER PRIMARY KEY AUTOINCREMENT,

title TEXT,

content TEXT,

file\_path TEXT,

created\_at TEXT

\- 提供增、查接口

\- 完善错误处理



4\. 文档导入

\- 支持 TXT / MD 格式

\- 菜单导入 + 拖拽导入

\- 读取内容存入数据库

\- 导入后自动刷新左侧列表



5\. 交互逻辑

\- 点击左侧文档 → 右侧显示内容

\- 状态栏实时显示总文档数

\- 界面布局合理、可缩放



=== CMake 必须配置 ===

cmake\_minimum\_required(VERSION 3.20)

project(OfflineKB)

set(CMAKE\_CXX\_STANDARD 17)

set(CMAKE\_PREFIX\_PATH "F:/msys2/mingw64")

find\_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

find\_package(SQLite3 REQUIRED)



=== 代码风格 ===

\- 头文件 .h，实现 .cpp

\- 类名 PascalCase，函数名 camelCase

\- 关键函数加注释

\- 数据库操作必须做异常处理

\- main.cpp 只保留启动逻辑

\- 代码干净、可直接编译运行



=== 输出要求 ===

请生成完整的以下文件，每个文件用文件名标注，代码放在代码块中：

1\. CMakeLists.txt

2\. main.cpp

3\. include/mainwindow.h

4\. src/mainwindow.cpp

5\. include/databasemanager.h

6\. src/databasemanager.cpp

7\. include/document.h



确保代码无报错、可直接编译运行。



\## 第二阶段：全文检索引擎

你是一名资深 C++ 架构师。请为"离线知识库系统"实现第二阶段——全文检索引擎。



=== 现有项目结构 ===

F:/OfflineKB/

├── CMakeLists.txt

├── main.cpp

├── include/

│   ├── mainwindow.h

│   ├── databasemanager.h

│   └── document.h

├── src/

│   ├── mainwindow.cpp

│   └── databasemanager.cpp

└── tests/



=== 技术约束 ===

\- C++17，Qt6，MinGW-w64，路径 F:/msys2/mingw64

\- 只生成新的代码文件，不要重复输出已有的文件

\- 新增依赖：cppjieba（中文分词库）

\- 代码必须线程安全

\- 所有注释使用中文

\- 支持中文搜索、停用词过滤



=== 需要生成的新文件 ===



1\. include/searchengine.h

\- 类名：SearchEngine

\- 成员方法：

&#x20; - void buildIndex(const std::vector<Document>\& docs) —— 构建倒排索引

&#x20; - std::vector<std::pair<int, double>> search(const std::string\& query) —— 返回文档ID+BM25分数，按分数降序

&#x20; - void addDocument(const Document\& doc) —— 增量添加单个文档

&#x20; - void removeDocument(int docId) —— 移除文档

\- 内部数据结构：

&#x20; - 倒排索引：unordered\_map<string, vector<pair<int, int>>> （词 → {文档ID, 词频}）

&#x20; - 文档长度缓存：unordered\_map<int, int> （文档ID → 总词数）

\- 加互斥锁，保证线程安全



2\. include/tokenizer.h

\- 类名：Tokenizer

\- 封装 cppjieba

\- 方法：std::vector<std::string> tokenize(const std::string\& text)

\- 过滤停用词（的、了、在、是、我、有、和、就、不、人、都、一、一个、上、也、很、到、说、要、去、你、会、着、没有、看、好、自己、这）

\- 返回去停用词后的词列表



3\. src/searchengine.cpp

\- 实现 SearchEngine 所有方法

\- buildIndex：遍历所有文档，对每个文档分词后构建倒排索引

\- search：对查询分词 → 查倒排索引 → 计算BM25分数 → 排序返回

\- BM25 参数：k1=1.5, b=0.75

\- 动态计算平均文档长度

\- 完整错误处理



4\. src/tokenizer.cpp

\- 实现 Tokenizer

\- 初始化 cppjieba 词典路径："resources/dict/"

\- 实现分词 + 停用词过滤



5\. tests/test\_search.cpp

\- 简单测试用例：插入文档，搜索关键词，验证结果



=== 需要修改的现有文件 ===

只输出修改部分！



6\. mainwindow.h 修改

\- 添加头文件：searchengine.h, tokenizer.h

\- 添加成员变量：SearchEngine\*, Tokenizer\*

\- 添加槽函数：void onSearch()

\- 添加方法：void refreshSearchResults(const std::vector<std::pair<int, double>>\& results)



7\. mainwindow.cpp 修改

\- 初始化 Tokenizer 和 SearchEngine

\- 连接搜索按钮点击信号到 onSearch

\- onSearch：获取搜索框文本 → 调用 search → 显示结果

\- 文档导入后自动 addDocument

\- 搜索结果显示在左侧列表



8\. CMakeLists.txt 修改

\- 添加 third\_party/cppjieba 头文件路径

\- 添加新 cpp 文件到编译

\- 链接 Qt6 Widgets Sql



=== 输出要求 ===

只输出新文件 + 现有文件的修改部分

每个文件用文件名标题 + 代码块

代码可直接编译运行，无报错



\## 分词问题修复

请帮我修复"离线知识库系统"的 tokenizer.cpp 和 searchengine.cpp，解决以下问题：



1\. 当前分词器对英文单词（如 CMakeLists）处理不正确，导致搜索不到。

2\. 需要确保中英文混合文本都能正确分词和搜索。



具体要求：



=== tokenizer.cpp 修改 ===

\- 在 tokenize 函数中，先用 QRegularExpression 提取英文单词和数字（正则：\\\\b\[A-Za-z0-9]+\\\\b）

\- 对中文部分用 cppjieba 分词

\- 对英文单词直接原样加入结果，并转小写

\- 合并所有结果返回

\- 保留停用词过滤



=== searchengine.cpp 修改 ===

\- buildIndex 时，不管中英文词，全部转小写再存入倒排索引

\- search 时，查询词也全部转小写



只输出需要修改的代码片段，不要整个文件。中文注释。

