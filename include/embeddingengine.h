#ifndef OFFLINEKB_EMBEDDINGENGINE_H
#define OFFLINEKB_EMBEDDINGENGINE_H

// =============================================================================
// EmbeddingEngine
// -----------------------------------------------------------------------------
// 轻量级文本向量编码器：cppjieba 分词 → 每词哈希种子 → 正态分布随机向量
// → 词向量逐维平均 → L2 归一化，输出固定 1024 维 float 向量。
//
// 支持两种初始化方式：
//   - init(Tokenizer*)           使用外部已构造的分词器，不负责释放
//   - init(const std::string&)   内部分词器，析构时自动 delete
// =============================================================================

#include <string>
#include <vector>

class Tokenizer;

class EmbeddingEngine {
public:
    EmbeddingEngine();
    ~EmbeddingEngine();

    EmbeddingEngine(const EmbeddingEngine&) = delete;
    EmbeddingEngine& operator=(const EmbeddingEngine&) = delete;

    // 使用外部 Tokenizer（生命周期由调用方管理）
    void init(Tokenizer* tokenizer);

    // 根据词典目录内部创建 Tokenizer（析构时释放）
    void init(const std::string& dictDir);

    // 将文本编码为 DIM 维向量；空文本或无有效分词时返回全零向量
    std::vector<float> encode(const std::string& text) const;

    int dimension() const { return DIM; }

private:
    // 由词字符串哈希种子生成单词随机向量（正态分布 N(0,1)）
    std::vector<float> hashWord(const std::string& word) const;

    // L2 归一化；范数过小则保持原样（避免除零）
    void normalize(std::vector<float>& vec) const;

    // 余弦相似度，维度不一致或分母过小时返回 0
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;

    Tokenizer* tokenizer_ = nullptr;
    bool ownTokenizer_ = false;

    static constexpr int DIM = 1024;
};

#endif  // OFFLINEKB_EMBEDDINGENGINE_H
