#include "searchengine.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace {
constexpr double kBm25K1 = 1.5;
constexpr double kBm25B = 0.75;

// 統一詞項大小寫，確保中英文檢索一致（中文不受影響）
std::string normalizeTerm(const std::string& term) {
    std::string lowered = term;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

// 在已加鎖情況下移除文檔索引
void removeDocumentUnlocked(
    int docId,
    std::unordered_map<std::string, std::vector<std::pair<int, int>>>& invertedIndex,
    std::unordered_map<int, int>& docLengths,
    std::unordered_map<int, std::unordered_map<std::string, int>>& docTermFreqs) {
    auto tfIt = docTermFreqs.find(docId);
    if (tfIt == docTermFreqs.end()) {
        docLengths.erase(docId);
        return;
    }

    for (const auto& [term, _] : tfIt->second) {
        auto postIt = invertedIndex.find(term);
        if (postIt == invertedIndex.end()) {
            continue;
        }
        auto& postings = postIt->second;
        postings.erase(
            std::remove_if(postings.begin(), postings.end(),
                           [docId](const auto& entry) { return entry.first == docId; }),
            postings.end());
        if (postings.empty()) {
            invertedIndex.erase(postIt);
        }
    }
    docTermFreqs.erase(tfIt);
    docLengths.erase(docId);
}
}  // namespace

SearchEngine::SearchEngine(const std::string& dictDir) : tokenizer_(dictDir) {}

void SearchEngine::buildIndex(const std::vector<Document>& docs) {
    std::lock_guard<std::mutex> lock(mutex_);

    invertedIndex_.clear();
    docLengths_.clear();
    docTermFreqs_.clear();

    try {
        for (const auto& doc : docs) {
            if (doc.id < 0) {
                continue;
            }

            // 使用 UTF-8，避免 Windows 下 toStdString 导致中文分词异常
            const QByteArray contentUtf8 = doc.content.toUtf8();
            const std::string content(contentUtf8.constData(), static_cast<size_t>(contentUtf8.size()));
            const auto terms = tokenizer_.tokenize(content);

            docLengths_[doc.id] = static_cast<int>(terms.size());
            auto& tfMap = docTermFreqs_[doc.id];

            for (const auto& term : terms) {
                const std::string normalized = normalizeTerm(term);
                if (normalized.empty()) {
                    continue;
                }
                ++tfMap[normalized];
            }

            for (const auto& [term, tf] : tfMap) {
                invertedIndex_[term].push_back({doc.id, tf});
            }
        }
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("建立索引失败: ") + ex.what());
    }
}
std::vector<std::pair<int, double>> SearchEngine::search(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int, double>> result;

    try {
        if (query.empty() || docLengths_.empty()) {
            return result;
        }

        const auto qTerms = tokenizer_.tokenize(query);
        if (qTerms.empty()) {
            return result;
        }

        std::unordered_map<int, double> scoreMap;
        const double avgDl = computeAvgDocLengthUnlocked();
        const double docCount = static_cast<double>(docLengths_.size());
        std::unordered_set<std::string> seenTerms;

        for (const auto& term : qTerms) {
            const std::string normalized = normalizeTerm(term);
            if (normalized.empty()) {
                continue;
            }

            if (!seenTerms.insert(normalized).second) {
                continue;
            }

            auto it = invertedIndex_.find(normalized);
            if (it == invertedIndex_.end()) {
                continue;
            }

            const auto& postings = it->second;
            const double df = static_cast<double>(postings.size());
            const double idf = std::log(1.0 + (docCount - df + 0.5) / (df + 0.5));

            for (const auto& [docId, tfInt] : postings) {
                const auto lenIt = docLengths_.find(docId);
                if (lenIt == docLengths_.end()) {
                    continue;
                }
                const double tf = static_cast<double>(tfInt);
                const double dl = static_cast<double>(lenIt->second);
                const double denom = tf + kBm25K1 * (1.0 - kBm25B + kBm25B * dl / avgDl);
                if (denom <= 0.0) {
                    continue;
                }
                const double score = idf * ((tf * (kBm25K1 + 1.0)) / denom);
                scoreMap[docId] += score;
            }
        }

        result.reserve(scoreMap.size());
        for (const auto& [docId, score] : scoreMap) {
            result.push_back({docId, score});
        }
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("全文检索失败: ") + ex.what());
    }

    return result;
}

void SearchEngine::addDocument(const Document& doc) {
    if (doc.id < 0) {
        throw std::runtime_error("新增索引失败: 文档ID无效");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        removeDocumentUnlocked(doc.id, invertedIndex_, docLengths_, docTermFreqs_);

        // 使用 UTF-8，避免 Windows 下 toStdString 导致中文分词异常
        const QByteArray contentUtf8 = doc.content.toUtf8();
        const std::string content(contentUtf8.constData(), static_cast<size_t>(contentUtf8.size()));
        const auto terms = tokenizer_.tokenize(content);

        docLengths_[doc.id] = static_cast<int>(terms.size());
        auto& tfMap = docTermFreqs_[doc.id];
        tfMap.clear();

        for (const auto& term : terms) {
            const std::string normalized = normalizeTerm(term);
            if (normalized.empty()) {
                continue;
            }
            ++tfMap[normalized];
        }

        for (const auto& [term, tf] : tfMap) {
            invertedIndex_[term].push_back({doc.id, tf});
        }
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("新增文档索引失败: ") + ex.what());
    }
}
void SearchEngine::removeDocument(int docId) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        removeDocumentUnlocked(docId, invertedIndex_, docLengths_, docTermFreqs_);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("移除文档索引失败: ") + ex.what());
    }
}

double SearchEngine::computeAvgDocLengthUnlocked() const {
    if (docLengths_.empty()) {
        return 1.0;
    }
    long long sum = 0;
    for (const auto& [_, len] : docLengths_) {
        sum += len;
    }
    const double avg = static_cast<double>(sum) / static_cast<double>(docLengths_.size());
    return avg > 0.0 ? avg : 1.0;
}