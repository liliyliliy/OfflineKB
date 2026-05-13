#ifndef OFFLINEKB_RAGENGINE_H
#define OFFLINEKB_RAGENGINE_H

// =============================================================================
// RagEngine
// -----------------------------------------------------------------------------
// 基于 llama.cpp C API 的本地 RAG 问答引擎封装。
//
//   - 加载 GGUF 量化模型，构建 llama_context 与采样链
//   - 采样参数：温度 0.1，top_p 0.9，重复惩罚 1.1
//   - ask(question, context) 同步返回回答字符串；耗时较长，调用方应在
//     worker 线程中调用以避免阻塞 Qt UI 线程
//   - stop() 可在另一线程被调用以请求中断当前 ask
//   - 公开方法通过 std::mutex 串行化，保证同一 llama_context 不会被并发使用
//   - 头文件不包含 llama.h，避免污染 Qt 翻译单元（仅前置声明）
// =============================================================================

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

// llama.cpp 内部类型的前置声明（具体定义见 third_party/llama.cpp/include/llama.h）
struct llama_model;
struct llama_context;
struct llama_sampler;

class RagEngine {
public:
    // 流式 token 回调；若设置则每生成一个 token 片段都会被调用
    using TokenCallback = std::function<void(const std::string& piece)>;

    RagEngine();
    ~RagEngine();

    RagEngine(const RagEngine&) = delete;
    RagEngine& operator=(const RagEngine&) = delete;

    // -------------------------------------------------------------------------
    // 加载本地 GGUF 模型并初始化 llama_context 与采样链
    //   modelPath GGUF 模型文件绝对路径
    // 异常：
    //   文件不存在、加载失败或上下文创建失败时抛出 std::runtime_error
    // -------------------------------------------------------------------------
    void init(const std::string& modelPath);

    // -------------------------------------------------------------------------
    // 同步问答：根据问题与检索到的上下文生成回答
    //   question 用户问题
    //   context  从知识库检索到的相关片段（可为空）
    // 返回：
    //   生成的完整回答字符串（已去除可能的尾部空白）
    // 说明：
    //   - 内部串行执行，多线程并发调用会被锁串行化
    //   - 若 stop() 被触发，函数会尽快返回已生成内容
    // 异常：
    //   未初始化、提示词溢出上下文或解码失败时抛出 std::runtime_error
    // -------------------------------------------------------------------------
    std::string ask(const std::string& question, const std::string& context);

    // -------------------------------------------------------------------------
    // 请求中断当前正在执行的 ask；不会等待 ask 真正返回。
    // 多次调用安全；线程安全。
    // -------------------------------------------------------------------------
    void stop();

    // -------------------------------------------------------------------------
    // 设置流式 token 回调（可选）
    //   cb 每次采样到一个 token 后被调用；传空函数即可关闭
    // 说明：
    //   回调在调用 ask 的线程中执行，请勿在回调中再次调用 ask/stop
    // -------------------------------------------------------------------------
    void setTokenCallback(TokenCallback cb);

    // -------------------------------------------------------------------------
    // 最近一次 ask() 的运行指标快照（由 ask 内部更新）
    //   prefillMs   prompt 解码总耗时（毫秒）
    //   decodeMs    生成阶段总耗时（毫秒）
    //   promptTokens prompt 实际 tokenize 后长度
    //   generatedTokens 已成功喂回上下文的生成 token 数
    //   tokensPerSecond 生成阶段 tokens/s（仅 decode 阶段，不含 prefill）
    // -------------------------------------------------------------------------
    struct LastMetrics {
        double prefillMs = 0.0;
        double decodeMs = 0.0;
        int    promptTokens = 0;
        int    generatedTokens = 0;
        double tokensPerSecond = 0.0;
        std::string modelPath;
        int    nGpuLayers = 0;
    };

    // 复制返回上一次 ask() 的指标快照；线程安全
    LastMetrics lastMetrics() const;

    // -------------------------------------------------------------------------
    // 配置项 setter（必须在 init() 之前调用才生效）
    // -------------------------------------------------------------------------
    // 卸载到 GPU 的层数；99 = 全部；0 = 纯 CPU
    void setNGpuLayers(int n)        { nGpuLayers_ = n; }
    // 上下文窗口大小（token）
    void setNCtx(int n)              { nCtx_ = n; }
    // 单次最大生成 token 数
    void setNPredict(int n)          { nPredict_ = n; }
    // n_batch（>= 单次 prompt 分片大小）
    void setNBatch(int n)            { nBatch_ = n; }
    // 是否启用 Flash Attention
    void setFlashAttn(bool on)       { flashAttn_ = on; }
    // 系统提示词（角色定义 + 行为约束）；ask 内部会通过 chat template 注入
    void setSystemPrompt(const std::string& s) { systemPrompt_ = s; }

private:
    // llama.cpp 资源（裸指针，由本类负责释放）
    llama_model*   model_   = nullptr;
    llama_context* ctx_     = nullptr;
    llama_sampler* sampler_ = nullptr;

    // 串行化所有公开方法（stop 除外）
    std::mutex mutex_;

    // 中断标志（stop 设为 true，ask 循环检测后提前返回）
    std::atomic<bool> stopRequested_{false};

    // 是否已成功 init
    bool initialized_ = false;

    // 上下文窗口与最大生成 token 数
    int nCtx_     = 8192;
    int nPredict_ = 2048;

    // GPU offload 层数；99 = 全部；0 = 纯 CPU
    int  nGpuLayers_ = 99;
    // n_batch / n_ubatch
    int  nBatch_     = 2048;
    // Flash Attention（CPU 不支持时 llama.cpp 会自动忽略）
    bool flashAttn_  = true;

    // 系统提示词；通过 chat template 注入到 system role
    std::string systemPrompt_ =
        "你是一个严谨的中文文档问答助手。"
        "你的任务是基于用户提供的【文档】内容回答【问题】，必须严格遵守以下规则："
        "1) 只使用【文档】中的事实，不得引入外部知识；"
        "2) 回答必须简洁、连贯；简单求答案的问题只给答案和简短依据，总结类问题才条目式展开；"
        "3) 禁止逐字复制【文档】里的题目原文、选项或填空；"
        "4) 若【文档】中没有相关信息，仅回答四个字：无法回答。";

    // 流式回调（可选）
    TokenCallback tokenCb_;

    // 最近一次 ask 的指标，由 ask 在 mutex_ 内部更新；
    // 通过 lastMetrics() 在 mutex_ 内拷贝读取，避免数据竞争
    LastMetrics lastMetrics_;

    // 当前模型路径（init 时记录），便于打日志/调试
    std::string modelPath_;
};

#endif  // OFFLINEKB_RAGENGINE_H
