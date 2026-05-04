#include "embeddingengine.h"

#include "tokenizer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr float kEps = 1e-12f;
}

EmbeddingEngine::EmbeddingEngine() = default;

EmbeddingEngine::~EmbeddingEngine() {
    if (ownTokenizer_) {
        delete tokenizer_;
        tokenizer_ = nullptr;
        ownTokenizer_ = false;
    }
}

void EmbeddingEngine::init(Tokenizer* tokenizer) {
    if (tokenizer == nullptr) {
        throw std::runtime_error("EmbeddingEngine::init: tokenizer 指针不能为空");
    }

    if (ownTokenizer_) {
        delete tokenizer_;
        ownTokenizer_ = false;
    }

    tokenizer_ = tokenizer;
    ownTokenizer_ = false;
}

void EmbeddingEngine::init(const std::string& dictDir) {
    if (dictDir.empty()) {
        throw std::runtime_error("EmbeddingEngine::init: 词典目录路径不能为空");
    }

    if (ownTokenizer_) {
        delete tokenizer_;
        tokenizer_ = nullptr;
        ownTokenizer_ = false;
    }

    try {
        tokenizer_ = new Tokenizer(dictDir);
        ownTokenizer_ = true;
    } catch (const std::exception& ex) {
        tokenizer_ = nullptr;
        ownTokenizer_ = false;
        throw std::runtime_error(std::string("EmbeddingEngine::init 创建 Tokenizer 失败: ") + ex.what());
    } catch (...) {
        tokenizer_ = nullptr;
        ownTokenizer_ = false;
        throw std::runtime_error("EmbeddingEngine::init 创建 Tokenizer 失败: 未知异常");
    }
}

std::vector<float> EmbeddingEngine::encode(const std::string& text) const {
    if (tokenizer_ == nullptr) {
        throw std::runtime_error("EmbeddingEngine::encode: 尚未初始化，请先调用 init()");
    }

    if (text.empty()) {
        return std::vector<float>(DIM, 0.0f);
    }

    std::vector<std::string> words;
    try {
        words = tokenizer_->tokenize(text);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("EmbeddingEngine::encode 分词失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("EmbeddingEngine::encode 分词失败: 未知异常");
    }

    if (words.empty()) {
        return std::vector<float>(DIM, 0.0f);
    }

    std::vector<float> sum(static_cast<size_t>(DIM), 0.0f);

    try {
        for (const auto& w : words) {
            const std::vector<float> vw = hashWord(w);
            for (int i = 0; i < DIM; ++i) {
                sum[static_cast<size_t>(i)] += vw[static_cast<size_t>(i)];
            }
        }
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("EmbeddingEngine::encode 生成词向量失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("EmbeddingEngine::encode 生成词向量失败: 未知异常");
    }

    const float inv = 1.0f / static_cast<float>(words.size());
    for (float& v : sum) {
        v *= inv;
    }

    normalize(sum);
    return sum;
}

std::vector<float> EmbeddingEngine::hashWord(const std::string& word) const {
    std::vector<float> v(static_cast<size_t>(DIM), 0.0f);

    const std::size_t seed = std::hash<std::string>{}(word);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));

    std::normal_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < DIM; ++i) {
        float x = dist(rng);
        if (!std::isfinite(x)) {
            x = 0.0f;
        }
        v[static_cast<size_t>(i)] = x;
    }

    return v;
}

void EmbeddingEngine::normalize(std::vector<float>& vec) const {
    if (vec.size() != static_cast<size_t>(DIM)) {
        throw std::runtime_error("EmbeddingEngine::normalize: 向量维度必须为 DIM");
    }

    double sq = 0.0;
    for (float x : vec) {
        sq += static_cast<double>(x) * static_cast<double>(x);
    }

    const double norm = std::sqrt(sq);
    if (!std::isfinite(norm) || norm < static_cast<double>(kEps)) {
        return;
    }

    const float inv = static_cast<float>(1.0 / norm);
    for (float& x : vec) {
        x *= inv;
    }
}

float EmbeddingEngine::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const {
    if (a.size() != b.size()) {
        return 0.0f;
    }

    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;

    for (size_t i = 0; i < a.size(); ++i) {
        const double xa = static_cast<double>(a[i]);
        const double xb = static_cast<double>(b[i]);
        dot += xa * xb;
        na += xa * xa;
        nb += xb * xb;
    }

    const double da = std::sqrt(na);
    const double db = std::sqrt(nb);

    if (!std::isfinite(da) || !std::isfinite(db) || !std::isfinite(dot)) {
        return 0.0f;
    }

    const double denom = da * db;
    if (denom < static_cast<double>(kEps)) {
        return 0.0f;
    }

    const double cosv = dot / denom;
    if (!std::isfinite(cosv)) {
        return 0.0f;
    }

    if (cosv > 1.0) {
        return 1.0f;
    }
    if (cosv < -1.0) {
        return -1.0f;
    }

    return static_cast<float>(cosv);
}
