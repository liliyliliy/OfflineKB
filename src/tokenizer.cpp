#include "tokenizer.h"

#include "cppjieba/Jieba.hpp"

#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <cctype>
#include <stdexcept>

Tokenizer::Tokenizer(const std::string& dictDir) {
    const std::string dictPath = dictDir + "/jieba.dict.utf8";
    const std::string hmmPath = dictDir + "/hmm_model.utf8";
    const std::string userDictPath = dictDir + "/user.dict.utf8";
    const std::string idfPath = dictDir + "/idf.utf8";
    const std::string stopWordPath = dictDir + "/stop_words.utf8";

    try {
        jieba_ = std::make_unique<cppjieba::Jieba>(dictPath, hmmPath, userDictPath, idfPath, stopWordPath);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("初始化 cppjieba 失败: ") + ex.what());
    }

    // 內建中文停用詞
    stopWords_ = {"的", "了", "在", "是", "我", "有", "和", "就", "不", "人", "都", "一", "一个",
                  "上", "也", "很", "到", "说", "要", "去", "你", "会", "着", "没有", "看", "好", "自己", "这"};
}

Tokenizer::~Tokenizer() = default;

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    if (text.empty()) {
        return {};
    }

    if (!jieba_) {
        throw std::runtime_error("分词器尚未初始化");
    }

    std::vector<std::string> filtered;
    const QString input = QString::fromUtf8(text.c_str());

    // 先提取英文與數字詞（如 CMakeLists、Qt6），並轉小寫
    const QRegularExpression enWordRegex("\\b[A-Za-z0-9]+\\b");
    QRegularExpressionMatchIterator it = enWordRegex.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        std::string token = match.captured(0).toStdString();

        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (token.empty()) {
            continue;
        }

        // 保留停用詞過濾
        if (stopWords_.find(token) != stopWords_.end()) {
            continue;
        }

        filtered.push_back(token);
    }

    // 中文內容使用 cppjieba 分詞
    std::vector<std::string> words;
    jieba_->CutForSearch(text, words);

 
    for (const auto& w : words) {
        if (w.empty()) {
            continue;
        }

        // 純英文/數字詞已在上面提取，這裡跳過避免重複
        const QString qw = QString::fromUtf8(w.c_str());
        if (enWordRegex.match(qw).hasMatch()) {
            continue;
        }

        // 保留停用詞過濾
        if (stopWords_.find(w) != stopWords_.end()) {
            continue;
        }

        filtered.push_back(w);
    }

    return filtered;
}