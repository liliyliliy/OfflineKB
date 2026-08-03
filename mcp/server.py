"""OfflineKB MCP Server - exposes local knowledge base as MCP Tools."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

# 确保能 import 同目录的 offlinekb_client
_SCRIPT_DIR = str(Path(__file__).resolve().parent)
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from mcp.server import MCPServer

from offlinekb_client import OfflineKbClientError, get_client

server = MCPServer("offlinekb")


def _json_text(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2)


@server.tool()
def list_documents() -> str:
    """列出 OfflineKB 中已导入的全部文档（id、标题、路径）。"""
    result = get_client().call("list_documents")
    return _json_text(result)


@server.tool()
def search_kb(query: str, mode: str = "keyword", top_k: int = 10) -> str:
    """在本地知识库中检索相关 chunk。

    Args:
        query: 检索关键词或自然语言问题
        mode: keyword（BM25 关键词）或 semantic（向量语义）
        top_k: 返回条数，默认 10
    """
    result = get_client().call(
        "search_kb",
        {"query": query, "mode": mode, "top_k": top_k},
        timeout=60.0,
    )
    return _json_text(result)


@server.tool()
def get_chunk(chunk_id: int) -> str:
    """根据 chunk_id 获取原文片段，用于溯源。"""
    result = get_client().call("get_chunk", {"chunk_id": chunk_id}, timeout=30.0)
    return _json_text(result)


@server.tool()
def ask_rag(question: str, doc_id: int | None = None) -> str:
    """基于本地知识库进行 RAG 问答（混合召回 + 本地 LLM 生成）。

    注意：本地 GGUF 模型推理可能需要 30 秒到 2 分钟。

    Args:
        question: 用户问题
        doc_id: 可选，聚焦指定文档 id
    """
    params: dict[str, Any] = {"question": question}
    if doc_id is not None:
        params["doc_id"] = doc_id
    result = get_client().call("ask_rag", params, timeout=600.0)
    return _json_text(result)


def main() -> None:
    # 不在启动时加载模型，避免 Cursor MCP 连接超时；首次 Tool 调用时再启动 CLI。
    server.run()


if __name__ == "__main__":
    main()
