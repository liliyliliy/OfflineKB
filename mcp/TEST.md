# CLI 手动测试步骤

在 `offlinekb-cli` 编译完成后，可用以下命令验证 JSON Lines 协议。

## PowerShell 交互测试

```powershell
$cli = "F:\OfflineKB\build\offlinekb-cli.exe"
$models = "F:\OfflineKB\models"

# 启动 server（会加载模型，需等待就绪）
$p = Start-Process -FilePath $cli -ArgumentList "server","--models-dir",$models -RedirectStandardInput "test_in.txt" -RedirectStandardOutput "test_out.txt" -RedirectStandardError "test_err.txt" -NoNewWindow -PassThru

# 或使用管道（推荐在 Git Bash / WSL 中测试）：
# echo '{"id":1,"method":"list_documents","params":{}}' | offlinekb-cli.exe server
```

## 测试用例 JSON（逐行发送）

```json
{"id":1,"method":"status","params":{}}
{"id":2,"method":"list_documents","params":{}}
{"id":3,"method":"search_kb","params":{"query":"RAG","mode":"keyword","top_k":5}}
{"id":4,"method":"get_chunk","params":{"chunk_id":1}}
{"id":5,"method":"ask_rag","params":{"question":"什么是 RAG？"}}
{"id":6,"method":"shutdown","params":{}}
```

## 预期

| id | method | 预期 |
|----|--------|------|
| 1 | status | `rag_ready` / `search_ready` 等字段 |
| 2 | list_documents | `documents` 数组 |
| 3 | search_kb | `hits` 数组，含 chunk_id/score |
| 4 | get_chunk | chunk 原文 |
| 5 | ask_rag | `answer` + `sources`（需 RAG 模型） |
| 6 | shutdown | `status: bye` |

## MCP 联调

1. `pip install -r mcp/requirements.txt`
2. 复制 `.cursor/mcp.json.example` 为 `.cursor/mcp.json` 并修改路径
3. 重启 Cursor，确认 MCP 面板 `offlinekb` 在线
4. 在对话中让 Agent 调用 `list_documents` 或 `search_kb`
