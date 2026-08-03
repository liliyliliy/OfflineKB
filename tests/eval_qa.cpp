// eval_qa.cpp — RagEngine-only smoke tests (loads LLM). For retrieval regression without
// LLM, see tests/eval_retrieval.cpp.
// 离线问答评测：给定一组固定的 (上下文、问题、期望关键词) 三元组，
// 跑 RagEngine.ask() 并按"包含关键词集中的任一关键词"做硬性 PASS/FAIL 判定。
//
// 设计目标：
//   - 让任何一次模型/采样/prompt/template 调整都能用同样的输入复现；
//   - 用本地构建出来的 RagEngine 二进制做 smoke test，避免回归（例如以前
//     发生过的 prompt 被截断、复读原文、KV cache 没清等问题）；
//   - 不引入任何在线依赖；
//   - 评测用例覆盖 4 个核心场景：
//       1) 摘要：要求把一段文本简化为短句
//       2) 事实问答：从段落里找事实
//       3) 无法回答：上下文里根本没有答案，模型应回退到"无法回答"
//       4) 跨段落：答案要靠综合两段才能拿到。
//
// 用法：
//   eval_qa <gguf 模型路径>
//
// 输出：每条用例打印 [PASS] / [FAIL] 和模型回答；最后给出汇总通过率与
// 平均 prefill / decode 耗时和 tokens/s。
// =============================================================================

#include "ragengine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

struct EvalCase {
    const char* name;
    const char* context;
    const char* question;
    // 任一命中即视为通过；用 "*" 表示无关键词检查（仅看模型不要崩）
    std::vector<std::string> mustContainAny;
    // 出现以下任一字符串视为失败（用于检测"复读原文"等回归）
    std::vector<std::string> mustNotContain;
};

bool containsAny(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& n : needles) {
        if (n == "*") return true;
        if (haystack.find(n) != std::string::npos) return true;
    }
    return false;
}

bool containsNone(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& n : needles) {
        if (haystack.find(n) != std::string::npos) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s <gguf 模型路径>\n", argv[0]);
        return 2;
    }

    const std::string modelPath = argv[1];

    // 评测集（全中文，故意写得简洁，保证模型在 4B 级别也能稳定通过）
    const std::vector<EvalCase> cases = {
        {
            "summary",
            "光合作用是绿色植物利用太阳能、二氧化碳和水合成有机物并释放氧气的过程。"
            "它是地球上几乎所有生命能量的最初来源。",
            "请用一句话概括这段文字讲了什么。",
            { "光合作用" },
            { "请用一句话概括这段文字讲了什么" },  // 不允许复读问题
        },
        {
            "fact-direct",
            "鲁迅原名周树人，是中国现代文学的奠基人之一，1881 年出生于浙江绍兴，"
            "代表作有《狂人日记》《阿 Q 正传》等。",
            "鲁迅出生于哪一年？",
            { "1881" },
            {},
        },
        {
            "fact-place",
            "鲁迅原名周树人，是中国现代文学的奠基人之一，1881 年出生于浙江绍兴。",
            "鲁迅是哪里人？",
            { "绍兴", "浙江" },
            {},
        },
        {
            "cannot-answer",
            "苹果公司由史蒂夫·乔布斯、史蒂夫·沃兹尼亚克和罗纳德·韦恩于 1976 年创立，"
            "总部位于美国加利福尼亚州库比蒂诺。",
            "苹果公司 2023 年的全年净利润是多少？",
            { "无法回答", "未提供", "没有", "未提及", "无法" },
            {},
        },
        {
            "cross-paragraph",
            "段落一：北京是中国的首都，常住人口超过 2000 万。\n\n"
            "段落二：上海是中国最大的金融中心，2023 年 GDP 约 4.7 万亿元人民币。\n\n"
            "段落三：广州是粤港澳大湾区的核心城市之一。",
            "上面的三个段落分别介绍了哪三座城市？",
            { "北京" },
            {},
        },
        {
            "cross-paragraph-2",
            "段落一：北京是中国的首都，常住人口超过 2000 万。\n\n"
            "段落二：上海是中国最大的金融中心，2023 年 GDP 约 4.7 万亿元人民币。",
            "在上文中，金融中心是哪座城市？",
            { "上海" },
            { "北京" },
        },
    };

    RagEngine engine;
    try {
        engine.setNGpuLayers(99);
        engine.setNCtx(4096);
        engine.setNPredict(192);
        engine.init(modelPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[FATAL] RagEngine 初始化失败: %s\n", ex.what());
        return 3;
    }

    int passed = 0;
    double totalPrefill = 0.0;
    double totalDecode = 0.0;
    double totalTps = 0.0;
    int    totalGenerated = 0;
    int    runs = 0;

    for (const auto& c : cases) {
        std::string answer;
        std::string err;
        try {
            answer = engine.ask(c.question, c.context);
        } catch (const std::exception& ex) {
            err = ex.what();
        } catch (...) {
            err = "未知异常";
        }

        const RagEngine::LastMetrics m = engine.lastMetrics();
        totalPrefill += m.prefillMs;
        totalDecode  += m.decodeMs;
        totalTps     += m.tokensPerSecond;
        totalGenerated += m.generatedTokens;
        ++runs;

        const bool ok = err.empty()
                        && containsAny(answer, c.mustContainAny)
                        && containsNone(answer, c.mustNotContain);

        std::printf("\n=== [%s] %s ===\n", ok ? "PASS" : "FAIL", c.name);
        std::printf("Q : %s\n", c.question);
        if (!err.empty()) {
            std::printf("ERR: %s\n", err.c_str());
        } else {
            std::printf("A : %s\n", answer.c_str());
            std::printf("Stats: prompt=%d gen=%d prefill=%.0fms decode=%.0fms tps=%.2f\n",
                        m.promptTokens, m.generatedTokens,
                        m.prefillMs, m.decodeMs, m.tokensPerSecond);
        }
        if (ok) ++passed;
    }

    const int total = static_cast<int>(cases.size());
    std::printf("\n--------------------------------\n");
    std::printf("评测结果: %d / %d 通过 (%.1f%%)\n",
                passed, total, 100.0 * passed / std::max(1, total));
    if (runs > 0) {
        std::printf("平均 prefill: %.0f ms\n", totalPrefill / runs);
        std::printf("平均 decode : %.0f ms\n", totalDecode  / runs);
        std::printf("平均 tps    : %.2f tok/s\n", totalTps / runs);
        std::printf("总 generated tokens: %d\n", totalGenerated);
    }

    return (passed == total) ? 0 : 1;
}
