#include "vectorindex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

VectorIndex::VectorIndex() = default;

VectorIndex::~VectorIndex() = default;

void VectorIndex::init(int dim, int maxElements) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (dim <= 0) {
        throw std::runtime_error("VectorIndex::init: 向量维度 dim 必须为正数");
    }
    if (maxElements <= 0) {
        throw std::runtime_error("VectorIndex::init: maxElements 必须为正数");
    }

    index_.reset();
    space_.reset();

    try {
        space_ = std::make_unique<hnswlib::L2Space>(static_cast<size_t>(dim));
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            space_.get(),
            static_cast<size_t>(maxElements),
            kM,
            kEfConstruction,
            100U /* random_seed */,
            false /* allow_replace_deleted */);
        index_->setEf(kEfSearch);
    } catch (const std::exception& ex) {
        index_.reset();
        space_.reset();
        initialized_ = false;
        dim_ = 0;
        throw std::runtime_error(std::string("VectorIndex::init 失败: ") + ex.what());
    } catch (...) {
        index_.reset();
        space_.reset();
        initialized_ = false;
        dim_ = 0;
        throw std::runtime_error("VectorIndex::init 失败: 未知异常");
    }

    dim_ = dim;
    initialized_ = true;
}

void VectorIndex::addVectors(const std::vector<float>& vec, int id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !index_ || !space_) {
        throw std::runtime_error("VectorIndex::addVectors: 索引尚未初始化，请先调用 init()");
    }

    if (static_cast<int>(vec.size()) != dim_) {
        throw std::runtime_error(
            "VectorIndex::addVectors: 向量维度不匹配，期望 " + std::to_string(dim_) +
            "，实际 " + std::to_string(vec.size()));
    }

    if (id < 0) {
        throw std::runtime_error("VectorIndex::addVectors: id 不能为负数");
    }

    const size_t cur = index_->cur_element_count.load();
    if (cur >= index_->max_elements_) {
        throw std::runtime_error("VectorIndex::addVectors: 索引已满，无法继续添加向量");
    }

    try {
        index_->addPoint(vec.data(), static_cast<hnswlib::labeltype>(id), false);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("VectorIndex::addVectors 失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("VectorIndex::addVectors 失败: 未知异常");
    }
}

std::vector<std::pair<int, float>> VectorIndex::search(const std::vector<float>& query, int k) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !index_ || !space_) {
        throw std::runtime_error("VectorIndex::search: 索引尚未初始化，请先调用 init()");
    }

    if (k <= 0) {
        return {};
    }

    if (static_cast<int>(query.size()) != dim_) {
        throw std::runtime_error(
            "VectorIndex::search: 查询向量维度不匹配，期望 " + std::to_string(dim_) +
            "，实际 " + std::to_string(query.size()));
    }

    const size_t curCount = index_->cur_element_count.load();
    if (curCount == 0) {
        return {};
    }

    const size_t kk = std::min(static_cast<size_t>(k), curCount);

    std::priority_queue<std::pair<float, hnswlib::labeltype>> pq;
    try {
        pq = index_->searchKnn(query.data(), kk, nullptr);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("VectorIndex::search 失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("VectorIndex::search 失败: 未知异常");
    }

    std::vector<std::pair<int, float>> out;
    out.reserve(pq.size());

    while (!pq.empty()) {
        const auto& top = pq.top();
        const float dist = top.first;
        const float similarity =
            (std::isfinite(dist) && dist >= 0.0f) ? (1.0f / (1.0f + dist)) : 0.0f;

        const hnswlib::labeltype label = top.second;
        if (label > static_cast<hnswlib::labeltype>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("VectorIndex::search: 标签值超出 int 可表示范围");
        }

        out.emplace_back(static_cast<int>(label), similarity);
        pq.pop();
    }

    // hnswlib 返回的堆先弹出较大距离；反转后得到相似度降序
    std::reverse(out.begin(), out.end());
    return out;
}

void VectorIndex::save(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !index_) {
        throw std::runtime_error("VectorIndex::save: 索引尚未初始化，请先调用 init()");
    }

    if (path.empty()) {
        throw std::runtime_error("VectorIndex::save: 文件路径不能为空");
    }

    try {
        index_->saveIndex(path);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("VectorIndex::save 失败: ") + ex.what());
    } catch (...) {
        throw std::runtime_error("VectorIndex::save 失败: 未知异常");
    }
}

void VectorIndex::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!space_) {
        throw std::runtime_error("VectorIndex::load: 请先调用 init() 创建度量空间（L2Space）");
    }

    if (path.empty()) {
        throw std::runtime_error("VectorIndex::load: 文件路径不能为空");
    }

    index_.reset();

    try {
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            space_.get(), path, false, 0, false);
        index_->setEf(kEfSearch);
    } catch (const std::exception& ex) {
        index_.reset();
        initialized_ = false;
        throw std::runtime_error(std::string("VectorIndex::load 失败: ") + ex.what());
    } catch (...) {
        index_.reset();
        initialized_ = false;
        throw std::runtime_error("VectorIndex::load 失败: 未知异常");
    }

    const size_t loadedDim = index_->data_size_ / sizeof(float);
    if (loadedDim == 0 || loadedDim > static_cast<size_t>(std::numeric_limits<int>::max())) {
        index_.reset();
        initialized_ = false;
        throw std::runtime_error("VectorIndex::load: 索引文件中的向量维度无效");
    }

    if (static_cast<int>(loadedDim) != dim_) {
        index_.reset();
        initialized_ = false;
        throw std::runtime_error(
            "VectorIndex::load: 文件中向量维度 (" + std::to_string(loadedDim) +
            ") 与当前 init 维度 (" + std::to_string(dim_) + ") 不一致");
    }

    initialized_ = true;
}

size_t VectorIndex::size() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !index_) {
        return 0;
    }
    return index_->cur_element_count.load();
}
