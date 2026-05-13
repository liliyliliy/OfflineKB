#include "embeddingengine.h"

#include "llama.h"
#include "tokenizer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr float kEps = 1e-12f;

std::once_flag g_embeddingLlamaBackendInitFlag;

void ensureEmbeddingLlamaBackend() {
    std::call_once(g_embeddingLlamaBackendInitFlag, []() {
        llama_backend_init();
    });
}
}

EmbeddingEngine::EmbeddingEngine() = default;

EmbeddingEngine::~EmbeddingEngine() {
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    if (ownTokenizer_) {
        delete tokenizer_;
        tokenizer_ = nullptr;
        ownTokenizer_ = false;
    }
}

bool EmbeddingEngine::initEmbeddingModel(const std::string& modelPath, int nGpuLayers) {
    if (modelPath.empty()) {
        return false;
    }

    ensureEmbeddingLlamaBackend();

    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    dim_ = DIM;
    modelName_ = "hash-embedding";

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = nGpuLayers;
    model_ = llama_model_load_from_file(modelPath.c_str(), mp);
    if (!model_) {
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx           = 1024;
    cp.n_batch         = 512;
    cp.n_ubatch        = 512;
    cp.n_threads       = static_cast<int32_t>(std::max(1u, std::thread::hardware_concurrency()));
    cp.n_threads_batch = cp.n_threads;
    cp.embeddings      = true;
    cp.pooling_type    = LLAMA_POOLING_TYPE_MEAN;
    cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    cp.offload_kqv     = true;

    ctx_ = llama_init_from_model(model_, cp);
    if (!ctx_) {
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    int32_t modelDim = llama_model_n_embd(model_);
    if (modelDim <= 0) {
        modelDim = llama_model_n_embd_out(model_);
    }
    if (modelDim <= 0) {
        llama_free(ctx_);
        llama_model_free(model_);
        ctx_ = nullptr;
        model_ = nullptr;
        dim_ = DIM;
        return false;
    }
    dim_ = modelDim;

    // 从路径中提取文件名作为模型标识
    auto slash = modelPath.find_last_of("/\\");
    modelName_ = (slash != std::string::npos) ? modelPath.substr(slash + 1) : modelPath;

    return true;
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
    if (tokenizer_ == nullptr && !(model_ && ctx_)) {
        throw std::runtime_error("EmbeddingEngine::encode: 尚未初始化，请先调用 init()");
    }

    if (text.empty()) {
        return std::vector<float>(static_cast<size_t>(dim_), 0.0f);
    }

    // Path A: 真实 embedding 模型（llama.cpp encode/decode + mean pooling）。
    if (model_ && ctx_) {
        const llama_vocab* vocab = llama_model_get_vocab(model_);
        if (!vocab) {
            throw std::runtime_error("EmbeddingEngine::encode: 无法获取 embedding 模型词表");
        }

        int32_t nNeeded = -llama_tokenize(
            vocab, text.c_str(), static_cast<int32_t>(text.size()),
            nullptr, 0, /*add_special=*/true, /*parse_special=*/true);
        if (nNeeded <= 0) {
            return std::vector<float>(static_cast<size_t>(dim_), 0.0f);
        }

        std::vector<llama_token> tokens(static_cast<size_t>(nNeeded));
        int32_t nTokens = llama_tokenize(
            vocab, text.c_str(), static_cast<int32_t>(text.size()),
            tokens.data(), static_cast<int32_t>(tokens.size()),
            /*add_special=*/true, /*parse_special=*/true);
        if (nTokens <= 0) {
            return std::vector<float>(static_cast<size_t>(dim_), 0.0f);
        }
        // 限制单次输入不超过 n_batch (=512)；超过时直接截断，避免越界
        if (nTokens > 512) {
            nTokens = 512;
        }
        tokens.resize(static_cast<size_t>(nTokens));

        // Embedding 不需要保留历史，每次清空 KV cache
        llama_memory_clear(llama_get_memory(ctx_), /*data=*/true);

        llama_batch batch = llama_batch_init(nTokens, /*embd=*/0, /*n_seq_max=*/1);
        for (int32_t i = 0; i < nTokens; ++i) {
            batch.token[i]   = tokens[static_cast<size_t>(i)];
            batch.pos[i]     = i;
            batch.n_seq_id[i]= 1;
            batch.seq_id[i][0] = 0;
            // embedding 模式下每个 token 都应标记为输出，否则 llama.cpp 会发出警告并强制覆盖。
            batch.logits[i]  = 1;
        }
        batch.n_tokens = nTokens;

        int32_t rc = llama_encode(ctx_, batch);
        if (rc != 0 && llama_model_has_decoder(model_)) {
            rc = llama_decode(ctx_, batch);
        }
        llama_batch_free(batch);
        if (rc != 0) {
            throw std::runtime_error(
                std::string("EmbeddingEngine::encode: 推理失败 rc=") + std::to_string(rc));
        }

        const float* emb = llama_get_embeddings_seq(ctx_, /*seq_id=*/0);
        if (!emb) {
            emb = llama_get_embeddings_ith(ctx_, -1);
        }
        if (!emb) {
            throw std::runtime_error("EmbeddingEngine::encode: 无法读取 embedding 输出");
        }

        std::vector<float> out(static_cast<size_t>(dim_), 0.0f);
        for (int i = 0; i < dim_; ++i) {
            const float v = emb[i];
            out[static_cast<size_t>(i)] = std::isfinite(v) ? v : 0.0f;
        }
        normalize(out);
        return out;
    }

    // Path B: 哈希词向量回退（与原实现一致，仅维度改为 dim_）
    std::vector<std::string> words;
    try {
        words = tokenizer_->tokenize(text);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("EmbeddingEngine::encode 分词失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("EmbeddingEngine::encode 分词失败: 未知异常");
    }

    if (words.empty()) {
        return std::vector<float>(static_cast<size_t>(dim_), 0.0f);
    }

    std::vector<float> sum(static_cast<size_t>(dim_), 0.0f);

    try {
        for (const auto& w : words) {
            const std::vector<float> vw = hashWord(w);
            for (int i = 0; i < dim_; ++i) {
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
    std::vector<float> v(static_cast<size_t>(dim_), 0.0f);

    const std::size_t seed = std::hash<std::string>{}(word);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));

    std::normal_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < dim_; ++i) {
        float x = dist(rng);
        if (!std::isfinite(x)) {
            x = 0.0f;
        }
        v[static_cast<size_t>(i)] = x;
    }

    return v;
}

void EmbeddingEngine::normalize(std::vector<float>& vec) const {
    if (vec.size() != static_cast<size_t>(dim_)) {
        throw std::runtime_error("EmbeddingEngine::normalize: 向量维度必须为 dim_");
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
