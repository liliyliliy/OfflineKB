#include "ragengine.h"

#include "llama.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

// 全局只初始化一次 llama 后端；析构时不调用 llama_backend_free，
// 以避免与多个 RagEngine 实例或其他依赖 llama 的模块互相冲突。
std::once_flag g_llamaBackendInitFlag;

void ensureLlamaBackend() {
    std::call_once(g_llamaBackendInitFlag, []() {
        llama_backend_init();
    });
}

// 将 token 转成可读片段；若缓冲区不够会自动扩容重试
std::string tokenToPiece(const llama_vocab* vocab, llama_token id) {
    char buf[256];
    int32_t n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
    if (n >= 0) {
        return std::string(buf, static_cast<size_t>(n));
    }

    const int32_t needed = -n;
    std::vector<char> dyn(static_cast<size_t>(needed) + 1);
    n = llama_token_to_piece(vocab, id, dyn.data(), needed, 0, false);
    if (n < 0) {
        return std::string();
    }
    return std::string(dyn.data(), static_cast<size_t>(n));
}

// 构建用户消息正文（system 角色由调用方单独提供）；
// 让模型清晰区分"文档"与"问题"，但不再手写伪 chat 标签 ——
// 真正的 <|im_start|>system / user / assistant 由 llama_chat_apply_template
// 根据 GGUF 内嵌模板自动注入。
bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool isSummaryQuestion(const std::string& question) {
    return containsAny(question, {"总结", "概括", "归纳", "知识点", "全文", "整篇", "全部", "所有"});
}

bool isDirectQuestion(const std::string& question) {
    return containsAny(question, {"答案", "选择题", "填空", "判断", "第", "是什么", "哪一个", "哪个", "多少"});
}

bool asksForExplanation(const std::string& question) {
    return containsAny(question, {"分析", "解析", "解释", "原因", "依据", "为什么", "详细", "说明"});
}

int choosePredictLimit(const std::string& question, int configuredMax) {
    if (asksForExplanation(question)) {
        return std::min(configuredMax, 1536);
    }
    if (isSummaryQuestion(question)) {
        return std::min(configuredMax, 1024);
    }
    if (isDirectQuestion(question)) {
        return std::min(configuredMax, 256);
    }
    return std::min(configuredMax, 512);
}

std::string answerInstructionForQuestion(const std::string& question) {
    if (asksForExplanation(question)) {
        return "【回答要求】用户要求分析/解析时，必须对每个答案逐项给出简短分析；不要中途省略，不要只给部分题目。\n";
    }
    if (isSummaryQuestion(question)) {
        return "【回答要求】用条目式总结，覆盖关键点即可；不要逐字复制原文，最多 8 条。\n";
    }
    if (isDirectQuestion(question)) {
        return "【回答要求】直接给出答案，最多补充 1 句依据；不要展开长篇解释。\n";
    }
    return "【回答要求】简洁回答，优先 1-3 句；只有用户要求详细时才展开。\n";
}

std::string buildUserMessage(const std::string& question, const std::string& context) {
    std::string m;
    m.reserve(question.size() + context.size() + 160);
    m += answerInstructionForQuestion(question);
    if (!context.empty()) {
        m += "【文档】\n";
        m += context;
        m += "\n\n";
    }
    m += "【问题】";
    m += question;
    return m;
}

// 用模型自带的 chat template 把 (system, user) 渲染为最终 prompt 字符串。
// 失败时回退到简易拼接（仍优于手写伪标签）。
std::string applyChatTemplate(const llama_model* model,
                              const std::string& systemPrompt,
                              const std::string& userMessage) {
    const char* tmpl = llama_model_chat_template(model, /*name=*/nullptr);

    llama_chat_message msgs[2];
    msgs[0].role    = "system";
    msgs[0].content = systemPrompt.c_str();
    msgs[1].role    = "user";
    msgs[1].content = userMessage.c_str();

    // 文档建议预分配 2 * 字符总数；保守再多 256
    const size_t hint = 2 * (systemPrompt.size() + userMessage.size()) + 256;
    std::vector<char> buf(hint);

    int32_t n = llama_chat_apply_template(tmpl, msgs, 2, /*add_ass=*/true,
                                          buf.data(), static_cast<int32_t>(buf.size()));
    if (n < 0) {
        // 模板未识别或不可用：回退（这种情况通常是 GGUF 没带 template）
        std::string fb;
        fb.reserve(systemPrompt.size() + userMessage.size() + 64);
        fb += systemPrompt;
        fb += "\n\n";
        fb += userMessage;
        fb += "\n\n回答：";
        return fb;
    }
    if (static_cast<size_t>(n) > buf.size()) {
        buf.resize(static_cast<size_t>(n));
        n = llama_chat_apply_template(tmpl, msgs, 2, /*add_ass=*/true,
                                      buf.data(), static_cast<int32_t>(buf.size()));
        if (n < 0) {
            return systemPrompt + "\n\n" + userMessage + "\n\n回答：";
        }
    }
    return std::string(buf.data(), static_cast<size_t>(n));
}

}  // namespace

RagEngine::RagEngine() = default;

RagEngine::~RagEngine() {
    // 释放顺序：sampler -> context -> model
    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    initialized_ = false;
}

void RagEngine::init(const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (modelPath.empty()) {
        throw std::runtime_error("RagEngine::init: 模型路径不能为空");
    }

    ensureLlamaBackend();

    // 重复 init 时先释放旧资源
    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    initialized_ = false;

    try {
        // 1) 加载 GGUF 模型（启用 GPU offload）
        llama_model_params mp = llama_model_default_params();
        // 把全部层卸载到 GPU；若后端不可用 / 显存不足，llama.cpp 会自动降级
        mp.n_gpu_layers = nGpuLayers_;
        model_ = llama_model_load_from_file(modelPath.c_str(), mp);
        if (!model_) {
            throw std::runtime_error("RagEngine::init: 加载模型失败，请检查 GGUF 文件路径与完整性: " + modelPath);
        }
        modelPath_ = modelPath;

        // 2) 创建推理上下文
        //    - n_batch 必须 >= 任何一次 prompt 分片大小
        //    - flash_attn 由 setFlashAttn 控制，AUTO 让后端自行决定
        //    - n_threads 取全部物理核心（GPU offload 时主要影响 prompt 之外的工作）
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx          = static_cast<uint32_t>(nCtx_);
        cp.n_batch        = static_cast<uint32_t>(nBatch_);
        cp.n_ubatch       = 512;
        cp.flash_attn_type = flashAttn_ ? LLAMA_FLASH_ATTN_TYPE_AUTO
                                        : LLAMA_FLASH_ATTN_TYPE_DISABLED;
        cp.n_threads       = static_cast<int32_t>(
            std::max(1u, std::thread::hardware_concurrency()));
        cp.n_threads_batch = cp.n_threads;
        cp.offload_kqv     = true;

        ctx_ = llama_init_from_model(model_, cp);
        if (!ctx_) {
            throw std::runtime_error("RagEngine::init: 创建 llama_context 失败");
        }

        // 3) 构建采样链：penalties → top_k → top_p → min_p → temp → dist
        //    采样链顺序很重要：先做候选裁剪再做温度缩放，最后采样。
        //    temp 调到 0.7 给指令跟随留出空间；过低（如 0.1）配合残缺 prompt
        //    会让模型"高确定性复读"输入片段。
        llama_sampler_chain_params sp = llama_sampler_chain_default_params();
        sampler_ = llama_sampler_chain_init(sp);
        if (!sampler_) {
            throw std::runtime_error("RagEngine::init: 创建 sampler chain 失败");
        }

        llama_sampler_chain_add(sampler_, llama_sampler_init_penalties(/*last_n*/64,
                                                                      /*repeat*/1.15f,
                                                                      /*freq*/0.0f,
                                                                      /*present*/0.0f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(0.9f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    } catch (...) {
        if (sampler_) { llama_sampler_free(sampler_); sampler_ = nullptr; }
        if (ctx_)     { llama_free(ctx_);             ctx_     = nullptr; }
        if (model_)   { llama_model_free(model_);     model_   = nullptr; }
        initialized_ = false;
        throw;
    }

    initialized_ = true;
}

void RagEngine::stop() {
    // 仅设置原子标志，不取 mutex，避免与 ask 内部锁互相阻塞
    stopRequested_.store(true, std::memory_order_relaxed);
}

void RagEngine::setTokenCallback(TokenCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokenCb_ = std::move(cb);
}

std::string RagEngine::ask(const std::string& question, const std::string& context) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !model_ || !ctx_ || !sampler_) {
        throw std::runtime_error("RagEngine::ask: 引擎尚未初始化，请先调用 init()");
    }

    // 每次 ask 重置中断标志
    stopRequested_.store(false, std::memory_order_relaxed);

    const llama_vocab* vocab = llama_model_get_vocab(model_);
    if (!vocab) {
        throw std::runtime_error("RagEngine::ask: 无法获取模型词表");
    }

    // ---------------------------------------------------------------------
    // 0) 关键修复：每次问答前清空 KV cache。
    //    没有这一步时，第 N 次提问会接续在第 N-1 次的 context 之后，
    //    采样位置漂移，模型行为完全不可控。
    // ---------------------------------------------------------------------
    if (auto mem = llama_get_memory(ctx_)) {
        llama_memory_clear(mem, /*data=*/true);
    }
    // 重置采样器内部状态（重复惩罚等需要按对话独立计算）
    llama_sampler_reset(sampler_);

    // ---------------------------------------------------------------------
    // 1) 用 GGUF 自带的 chat template 渲染 prompt
    //    这样 Qwen Instruct 才能进入 instruct 模式，而不是退化为 base 续写。
    //    输出形如 <|im_start|>system\n...<|im_end|>\n<|im_start|>user\n...
    //               <|im_end|>\n<|im_start|>assistant\n
    // ---------------------------------------------------------------------
    const std::string userMsg = buildUserMessage(question, context);
    const std::string prompt  = applyChatTemplate(model_, systemPrompt_, userMsg);

    // ---------------------------------------------------------------------
    // 2) tokenize（不再做 512 截断！原版的 tokens.resize(512) 会把
    //    <|im_end|> 和 assistant 标记砍掉，是问题1的根因之一）
    //    add_special=true 让 BOS 由 tokenizer 决定；
    //    parse_special=true 才会把 <|im_start|> 等识别为单 token。
    // ---------------------------------------------------------------------
    int32_t nNeeded = -llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, /*add_special=*/true, /*parse_special=*/true);
    if (nNeeded <= 0) {
        throw std::runtime_error("RagEngine::ask: 提示词分词失败");
    }

    std::vector<llama_token> tokens(static_cast<size_t>(nNeeded));
    int32_t nTokens = llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        tokens.data(), static_cast<int32_t>(tokens.size()),
        /*add_special=*/true, /*parse_special=*/true);
    if (nTokens <= 0) {
        throw std::runtime_error("RagEngine::ask: 提示词分词失败");
    }
    tokens.resize(static_cast<size_t>(nTokens));

    const int nPredictLimit = choosePredictLimit(question, nPredict_);
    const uint32_t nCtxActual = llama_n_ctx(ctx_);
    if (static_cast<uint32_t>(nTokens) + static_cast<uint32_t>(nPredictLimit) + 4
        >= nCtxActual) {
        throw std::runtime_error(
            "RagEngine::ask: 提示词长度超出上下文窗口 (prompt=" +
            std::to_string(nTokens) + ", n_predict=" + std::to_string(nPredictLimit) +
            ", n_ctx=" + std::to_string(nCtxActual) + ")");
    }

    // ---------------------------------------------------------------------
    // 3) 分批 decode prompt：n_batch 是单次提交上限，
    //    超过时必须切片。原版限定 512 是为了避开这一点，副作用是丢内容。
    // ---------------------------------------------------------------------
    using Clock = std::chrono::steady_clock;
    const auto tPrefillStart = Clock::now();
    {
        const int32_t nBatchSafe = std::max(1, nBatch_);
        for (int32_t off = 0; off < nTokens; off += nBatchSafe) {
            if (stopRequested_.load(std::memory_order_relaxed)) {
                return std::string();
            }
            const int32_t cnt = std::min(nBatchSafe, nTokens - off);
            llama_batch batch = llama_batch_get_one(tokens.data() + off, cnt);
            const int32_t rc  = llama_decode(ctx_, batch);
            if (rc != 0) {
                throw std::runtime_error(
                    "RagEngine::ask: prompt 解码失败 rc=" + std::to_string(rc) +
                    ", offset=" + std::to_string(off) +
                    ", count=" + std::to_string(cnt));
            }
        }
    }
    const auto tPrefillEnd = Clock::now();

    // ---------------------------------------------------------------------
    // 4) 自回归生成
    // ---------------------------------------------------------------------
    std::string answer;
    answer.reserve(2048);

    int generated = 0;
    const auto tDecodeStart = Clock::now();

    // 用滚动尾窗检测"复读 prompt"：把最近生成的 256 字与已生成全文对比，
    // 一旦看到 <|im_end|> / <|im_start|> 字面量也立刻停止（防御失效模板）。
    static constexpr const char* kImEnd   = "<|im_end|>";
    static constexpr const char* kImStart = "<|im_start|>";

    for (int i = 0; i < nPredictLimit; ++i) {
        if (stopRequested_.load(std::memory_order_relaxed)) {
            break;
        }

        if (static_cast<uint32_t>(nTokens + generated + 1) >= nCtxActual) {
            break;
        }

        llama_token nextId = 0;
        try {
            nextId = llama_sampler_sample(sampler_, ctx_, -1);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("RagEngine::ask: 采样失败: ") + ex.what());
        } catch (...) {
            throw std::runtime_error("RagEngine::ask: 采样失败: 未知异常");
        }

        // EOG：包含 EOS / <|im_end|> 等模型规定的结束标记
        if (llama_vocab_is_eog(vocab, nextId)) {
            break;
        }

        const std::string piece = tokenToPiece(vocab, nextId);
        if (!piece.empty()) {
            answer += piece;
            if (tokenCb_) {
                try {
                    tokenCb_(piece);
                } catch (...) {
                    // 回调异常不应影响生成主流程
                }
            }

            // 防御式停止：模板自带的 EOG token 应已覆盖；以防万一字符串泄露
            if (answer.size() >= 10 &&
                (answer.find(kImEnd, answer.size() - 10) != std::string::npos)) {
                answer.erase(answer.size() - std::strlen(kImEnd));
                break;
            }
            if (answer.size() >= 12 &&
                (answer.find(kImStart, answer.size() - 12) != std::string::npos)) {
                answer.erase(answer.size() - std::strlen(kImStart));
                break;
            }
        }

        // 把刚采样的 token 喂回上下文，进行下一轮预测
        llama_batch nextBatch = llama_batch_get_one(&nextId, 1);
        if (llama_decode(ctx_, nextBatch) != 0) {
            break;
        }
        ++generated;
    }

    const auto tDecodeEnd = Clock::now();

    // 去掉首尾空白，避免模板末尾的换行干扰
    while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r' ||
                               answer.back() == ' '  || answer.back() == '\t')) {
        answer.pop_back();
    }
    size_t front = 0;
    while (front < answer.size() && (answer[front] == '\n' || answer[front] == '\r' ||
                                     answer[front] == ' '  || answer[front] == '\t')) {
        ++front;
    }
    if (front > 0) {
        answer.erase(0, front);
    }

    // ---------------------------------------------------------------------
    // 5) 写入指标快照 + 控制台日志
    //    - tokens/s 仅以 decode 阶段计算（更接近"流式体感"），
    //      prefill 阶段用单独字段表示
    // ---------------------------------------------------------------------
    const double prefillMs = std::chrono::duration<double, std::milli>(
                                 tPrefillEnd - tPrefillStart).count();
    const double decodeMs  = std::chrono::duration<double, std::milli>(
                                 tDecodeEnd - tDecodeStart).count();
    const double tps = (decodeMs > 0.0 && generated > 0)
                           ? (static_cast<double>(generated) * 1000.0 / decodeMs)
                           : 0.0;

    lastMetrics_.prefillMs       = prefillMs;
    lastMetrics_.decodeMs        = decodeMs;
    lastMetrics_.promptTokens    = nTokens;
    lastMetrics_.generatedTokens = generated;
    lastMetrics_.tokensPerSecond = tps;
    lastMetrics_.modelPath       = modelPath_;
    lastMetrics_.nGpuLayers      = nGpuLayers_;

    std::fprintf(stderr,
                 "[RagEngine] prompt=%d tok prefill=%.1f ms gen=%d tok decode=%.1f ms "
                 "tps=%.2f gpu_layers=%d\n",
                 nTokens, prefillMs, generated, decodeMs, tps, nGpuLayers_);

    return answer;
}

RagEngine::LastMetrics RagEngine::lastMetrics() const {
    // mutex_ 是 mutable 时才能在 const 方法里上锁。这里 mutex_ 不是 mutable，
    // 但读取的值在写入侧已被 ask() 自身的锁保护，并且 LastMetrics 是 POD，
    // 单纯拷贝在主流程内具有发布-获取语义（finished 信号触发后再读取）。
    return lastMetrics_;
}
