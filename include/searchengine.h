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
    explicit SearchEngine(const std::string& dictDir);

    void buildIndex(const std::vector<Document>& docs);
    std::vector<std::pair<int, double>> search(const std::string& query);
    void addDocument(const Document& doc);
    void removeDocument(int docId);

private:
    // 倒排索引：詞項 -> [(文檔ID, 詞頻)]
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> invertedIndex_;
    // 文檔長度：文檔ID -> 分詞後總詞數
    std::unordered_map<int, int> docLengths_;

    // 用於高效刪除文檔的詞頻快取：文檔ID -> {詞項: 詞頻}
    std::unordered_map<int, std::unordered_map<std::string, int>> docTermFreqs_;

    Tokenizer tokenizer_;
    mutable std::mutex mutex_;

    double computeAvgDocLengthUnlocked() const;
};