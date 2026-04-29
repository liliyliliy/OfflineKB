#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace cppjieba {
class Jieba;
}

class Tokenizer {
public:
    explicit Tokenizer(const std::string& dictDir);
    ~Tokenizer();

    std::vector<std::string> tokenize(const std::string& text) const;

private:
    std::unique_ptr<cppjieba::Jieba> jieba_;
    std::unordered_set<std::string> stopWords_;
};