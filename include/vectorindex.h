#ifndef OFFLINEKB_VECTORINDEX_H
#define OFFLINEKB_VECTORINDEX_H

// =============================================================================
// VectorIndex
// -----------------------------------------------------------------------------
// 基于 hnswlib 的向量索引封装类。
//
// 使用 L2Space 作为度量空间，内部维护一个 hnswlib::HierarchicalNSW<float> 索引。
// 所有公开方法均通过互斥锁保证线程安全，可在多线程环境下安全调用。
//
// HNSW 关键参数：
//   - M               = 16   邻居图最大出度
//   - efConstruction  = 200  构图阶段候选集大小
//   - efSearch        = 100  查询阶段候选集大小
//
// 近邻搜索返回的 float 为相似度：1.0 / (1.0 + L2距离)，结果按相似度降序排列。
// =============================================================================

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// hnswlib 第三方库头文件（位于 third_party/hnswlib/hnswlib/）
#include "hnswlib/hnswlib.h"

class VectorIndex {
public:
    VectorIndex();
    ~VectorIndex();

    // 禁用拷贝构造与拷贝赋值，避免索引资源被错误复制
    VectorIndex(const VectorIndex&) = delete;
    VectorIndex& operator=(const VectorIndex&) = delete;

    // -------------------------------------------------------------------------
    // 初始化索引
    //   dim         向量维度，默认 1024
    //   maxElements 索引可容纳的最大向量数量，默认 100000
    // 说明：
    //   会创建 hnswlib::L2Space 与 hnswlib::HierarchicalNSW<float>，
    //   并设置 efSearch。重复调用会先释放旧索引再重新构建。
    // -------------------------------------------------------------------------
    void init(int dim = 1024, int maxElements = 100000);

    // -------------------------------------------------------------------------
    // 添加一个向量到索引
    //   vec 向量数据，长度必须等于初始化时传入的 dim
    //   id  向量的唯一标识（label），用于在搜索结果中返回
    // 异常：
    //   维度不匹配、索引未初始化或容量已满时抛出 std::runtime_error
    // -------------------------------------------------------------------------
    void addVectors(const std::vector<float>& vec, int id);

    // -------------------------------------------------------------------------
    // 近邻搜索
    //   query 查询向量，长度必须等于 dim
    //   k     返回的最近邻数量
    // 返回：
    //   std::vector<std::pair<int, float>>，每项为 (id, similarity)，
    //   similarity = 1.0 / (1.0 + L2距离)，按相似度降序（最相似在前）。
    //   若索引为空或 k <= 0，返回空 vector。
    // -------------------------------------------------------------------------
    std::vector<std::pair<int, float>> search(const std::vector<float>& query, int k);

    // -------------------------------------------------------------------------
    // 将索引保存到本地文件
    //   path 目标文件路径
    // 异常：
    //   索引未初始化或写入失败时抛出 std::runtime_error
    // -------------------------------------------------------------------------
    void save(const std::string& path);

    // -------------------------------------------------------------------------
    // 从本地文件加载索引
    //   path 索引文件路径
    // 说明：
    //   调用前需先 init() 以创建 L2Space（维度须与保存索引时一致）。
    // -------------------------------------------------------------------------
    void load(const std::string& path);

    // -------------------------------------------------------------------------
    // 返回当前已索引的向量数量
    // -------------------------------------------------------------------------
    size_t size() const;

private:
    // HNSW 构建参数
    static constexpr size_t kM = 16;                // 邻居图最大出度
    static constexpr size_t kEfConstruction = 200;  // 构图阶段候选集大小
    static constexpr size_t kEfSearch = 100;        // 查询阶段候选集大小

    int dim_ = 0;                 // 向量维度
    bool initialized_ = false;    // 是否已完成 init（含 load 成功后的状态）

    // L2 距离度量空间（必须在 index_ 之前声明，以便析构时先销毁 index_）
    std::unique_ptr<hnswlib::L2Space> space_;

    // HNSW 分层小世界图索引
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index_;

    // 全局互斥锁，保护所有公开方法的并发访问
    mutable std::mutex mutex_;
};

#endif  // OFFLINEKB_VECTORINDEX_H
