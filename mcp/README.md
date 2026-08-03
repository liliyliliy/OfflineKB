# OfflineKB MCP Server

将 OfflineKB 本地知识库暴露为 MCP Tool，供 Cursor 等 Agent 调用。

## 架构

```text
Cursor (MCP Client)
    -> Python mcp/server.py（协议适配，~100 行）
        -> offlinekb-cli server（C++ JSON Lines 常驻进程）
            -> KbService（检索 + RAG 核心）
```

Python 只做 MCP 胶水层；BM25、HNSW、llama.cpp 推理均在 C++ 中。

## 前置条件

0. **Windows 构建 PATH**：在 PowerShell 中编译前，需把 MSYS2 加入 PATH，否则 `uic.exe` 会因缺少 DLL 失败：

```powershell
$env:PATH = "F:/msys2/mingw64/bin;F:/msys2/mingw64/share/qt6/bin;" + $env:PATH
```

1. 已编译 `offlinekb-cli`：

```powershell
cd F:\OfflineKB\build
cmake ..
cmake --build . --target offlinekb-cli
```

2. 已用 GUI 导入文档（MCP 与 GUI 共用同一 SQLite 和向量索引）
3. 模型文件位于 `build/models/` 或 `models/`（与 GUI 相同）

## 安装 Python 依赖

请使用 **Python 3.12**（MSYS2 自带的 `python` 3.14 没有 pip/mcp 包）：

```powershell
& "C:/Program Files/Python312/python.exe" -m pip install -r requirements.txt
```

## Cursor 配置

在项目或用户目录创建/编辑 `.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "offlinekb": {
      "command": "C:/Program Files/Python312/python.exe",
      "args": ["F:/OfflineKB/mcp/server.py"],
      "env": {
        "OFFLINEKB_CLI": "F:/OfflineKB/build/offlinekb-cli.exe",
        "OFFLINEKB_MODELS_DIR": "F:/OfflineKB/models"
      }
    }
  }
}
```

重启 Cursor 后，在 MCP 面板应看到 `offlinekb` 在线。

## 可用 Tool

| Tool | 说明 |
|------|------|
| `list_documents` | 列出已导入文档 |
| `search_kb` | 关键词/语义检索 chunk |
| `get_chunk` | 按 id 获取 chunk 原文 |
| `ask_rag` | 混合召回 + 本地 LLM 问答 |

## 手动测试 CLI

```powershell
F:\OfflineKB\build\offlinekb-cli.exe server --models-dir F:\OfflineKB\models
```

另开终端，向 stdin 发送 JSON（一行一条）：

```json
{"id":1,"method":"list_documents","params":{}}
{"id":2,"method":"search_kb","params":{"query":"RAG","mode":"keyword","top_k":5}}
{"id":3,"method":"ask_rag","params":{"question":"什么是 RAG？"}}
{"id":4,"method":"shutdown","params":{}}
```

## 故障排查

| 现象 | 处理 |
|------|------|
| `找不到 offlinekb-cli` | 设置 `OFFLINEKB_CLI` 环境变量 |
| `RAG_NOT_READY` | 确认 `Qwen3-4B-Instruct-2507-Q4_K_M.gguf` 在 models 目录 |
| `ask_rag` 超时 | 本地 LLM 首次加载较慢，等待 1～2 分钟 |
| 检索无结果 | 先用 GUI 导入文档并确认索引已建立 |
