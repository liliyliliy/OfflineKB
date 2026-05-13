#pragma once

#include "document.h"
#include "tokenizer.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class SearchEngine {
public:
    struct ChunkRecord {
        int id = -1;
        int documentId = -1;
        QString title;
        QString content;
        QString filePath;
    };

    explicit SearchEngine(const std::string& dictDir);

    void buildIndex(const std::vector<Document>& docs);
    std::vector<std::pair<int, double>> search(const std::string& query);
    void addDocument(const Document& doc);
    void removeDocument(int docId);

    void buildChunkIndex(const std::vector<ChunkRecord>& chunks);
    std::vector<std::pair<int, double>> searchChunks(const std::string& query, int topK = 20);
    void addChunk(const ChunkRecord& chunk);
    void removeChunksForDocument(int documentId);

private:
    // 倒排索引：詞項 -> [(文檔ID, 詞頻)]
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> invertedIndex_;
    // 文檔長度：文檔ID -> 分詞後總詞數
    std::unordered_map<int, int> docLengths_;

    // 用於高效刪除文檔的詞頻快取：文檔ID -> {詞項: 詞頻}
    std::unordered_map<int, std::unordered_map<std::string, int>> docTermFreqs_;

    // Chunk 级倒排索引：词项 -> [(chunkID, 词频)]
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> chunkInvertedIndex_;
    // chunkID -> 分词后总词数
    std::unordered_map<int, int> chunkLengths_;
    // chunkID -> documentID，用于按文档删除 chunks
    std::unordered_map<int, int> chunkToDoc_;
    // chunkID -> {词项: 词频}
    std::unordered_map<int, std::unordered_map<std::string, int>> chunkTermFreqs_;

    Tokenizer tokenizer_;
    mutable std::mutex mutex_;

    double computeAvgDocLengthUnlocked() const;
    double computeAvgChunkLengthUnlocked() const;
};