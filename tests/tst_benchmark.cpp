/**
 * @file tst_benchmark.cpp
 * @brief IPCSharedDataPool 性能基准测试
 * 
 * 测试项目：
 * 1. 单点读取延迟
 * 2. 单点写入延迟
 * 3. 批量操作性能
 * 4. 并发读写性能
 * 5. 事件发布订阅延迟
 * 6. 内存占用
 * 7. 心跳与健康监控开销
 */

#include "../include/DataPoolClient.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace IPC;

// ========== 测试配置 ==========

static const char* TEST_POOL_NAME = "/ipc_bench_pool";
static const char* TEST_EVENT_NAME = "/ipc_bench_events";
static const char* TEST_SOE_NAME = "/ipc_bench_soe";

constexpr uint32_t WARMUP_ITERATIONS = 1000;
constexpr uint32_t BENCH_ITERATIONS = 1000000;
constexpr uint32_t BATCH_SIZE = 1000;
constexpr uint32_t CONCURRENT_THREADS = 4;

// ========== 工具函数 ==========

class Timer {
public:
    Timer() : m_start(std::chrono::high_resolution_clock::now()) {}
    
    void reset() {
        m_start = std::chrono::high_resolution_clock::now();
    }
    
    double elapsedNs() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::nano>(end - m_start).count();
    }
    
    double elapsedUs() const {
        return elapsedNs() / 1000.0;
    }
    
    double elapsedMs() const {
        return elapsedNs() / 1000000.0;
    }
    
private:
    std::chrono::high_resolution_clock::time_point m_start;
};

static void cleanupTestShm() {
    shm_unlink(TEST_POOL_NAME);
    shm_unlink(TEST_EVENT_NAME);
    shm_unlink(TEST_SOE_NAME);
}

static void printResult(const char* testName, double value, const char* unit) {
    std::cout << std::left << std::setw(40) << testName << ": " 
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) << value 
              << " " << unit << std::endl;
}

static void printThroughput(const char* testName, uint64_t ops, double seconds) {
    double throughput = ops / seconds / 1000000.0;  // M ops/s
    std::cout << std::left << std::setw(40) << testName << ": " 
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) << throughput 
              << " M ops/s" << std::endl;
}

// ========== 基准测试函数 ==========

void benchmarkSingleRead(DataPoolClient* client) {
    std::cout << "\n=== 单点读取延迟测试 ===\n";
    
    uint64_t key = makeKey(100, 1);
    uint32_t idx;
    client->registerPoint(key, PointType::YC, idx);
    client->setYC(key, 123.456f);
    
    // 预热
    float value;
    uint8_t quality;
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        client->getYC(key, value, quality);
    }
    
    // 测试通过 key 读取
    Timer timer;
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->getYC(key, value, quality);
    }
    double keyReadNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YC 读取 (by key)", keyReadNs, "ns/op");
    
    // 测试通过索引读取
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->getYCByIndex(idx, value, quality);
    }
    double idxReadNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YC 读取 (by index)", idxReadNs, "ns/op");
    
    // YX 读取
    uint64_t yxKey = makeKey(100, 100);
    uint32_t yxIdx;
    client->registerPoint(yxKey, PointType::YX, yxIdx);
    client->setYX(yxKey, 1);
    
    uint8_t yxValue;
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->getYXByIndex(yxIdx, yxValue, quality);
    }
    double yxReadNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YX 读取 (by index)", yxReadNs, "ns/op");
}

void benchmarkSingleWrite(DataPoolClient* client) {
    std::cout << "\n=== 单点写入延迟测试 ===\n";
    
    uint64_t key = makeKey(200, 1);
    uint32_t idx;
    client->registerPoint(key, PointType::YC, idx);
    
    // 预热
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        client->setYC(key, static_cast<float>(i));
    }
    
    // 测试通过 key 写入
    Timer timer;
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->setYC(key, static_cast<float>(i));
    }
    double keyWriteNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YC 写入 (by key)", keyWriteNs, "ns/op");
    
    // 测试通过索引写入
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->setYCByIndex(idx, static_cast<float>(i));
    }
    double idxWriteNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YC 写入 (by index)", idxWriteNs, "ns/op");
    
    // YX 写入
    uint64_t yxKey = makeKey(200, 100);
    uint32_t yxIdx;
    client->registerPoint(yxKey, PointType::YX, yxIdx);
    
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->setYXByIndex(yxIdx, static_cast<uint8_t>(i % 2));
    }
    double yxWriteNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("YX 写入 (by index)", yxWriteNs, "ns/op");
}

void benchmarkBatchOperations(DataPoolClient* client) {
    std::cout << "\n=== 批量操作性能测试 ===\n";
    
    SharedDataPool* pool = client->getDataPool();
    
    // 准备批量数据
    std::vector<uint64_t> keys(BATCH_SIZE);
    std::vector<float> values(BATCH_SIZE);
    std::vector<uint64_t> timestamps(BATCH_SIZE);
    
    for (uint32_t i = 0; i < BATCH_SIZE; i++) {
        keys[i] = makeKey(300, i);
        values[i] = static_cast<float>(i) * 1.1f;
        timestamps[i] = getCurrentTimestamp();
        uint32_t idx;
        pool->registerKey(keys[i], PointType::YC, idx);
    }
    
    // 预热
    for (uint32_t i = 0; i < 10; i++) {
        pool->batchSetYC(keys.data(), values.data(), timestamps.data(), BATCH_SIZE);
    }
    
    // 批量写入测试
    Timer timer;
    uint32_t batchIterations = BENCH_ITERATIONS / BATCH_SIZE;
    for (uint32_t i = 0; i < batchIterations; i++) {
        pool->batchSetYC(keys.data(), values.data(), timestamps.data(), BATCH_SIZE);
    }
    double totalOps = static_cast<double>(batchIterations) * BATCH_SIZE;
    printThroughput("批量 YC 写入 (1000点/批)", totalOps, timer.elapsedNs() / 1e9);
    
    double batchNs = timer.elapsedNs() / batchIterations;
    printResult("单批次延迟", batchNs / 1000.0, "us/batch");
    printResult("单点批量写入延迟", batchNs / BATCH_SIZE, "ns/point");
}

void benchmarkConcurrentAccess(DataPoolClient* client) {
    std::cout << "\n=== 并发读写性能测试 ===\n";
    
    const uint32_t POINTS_PER_THREAD = 1000;
    const uint32_t OPS_PER_THREAD = 500000;
    
    // 为每个线程注册点位
    for (uint32_t t = 0; t < CONCURRENT_THREADS; t++) {
        for (uint32_t i = 0; i < POINTS_PER_THREAD; i++) {
            uint64_t key = makeKey(400 + t, i);
            uint32_t idx;
            client->registerPoint(key, PointType::YC, idx);
        }
    }
    
    std::atomic<bool> start{false};
    std::atomic<uint64_t> totalOps{0};
    std::vector<std::thread> threads;
    
    auto workerFunc = [&](uint32_t /* threadId */) {
        // 等待开始信号
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        SharedDataPool* pool = client->getDataPool();
        uint64_t localOps = 0;
        
        for (uint32_t i = 0; i < OPS_PER_THREAD; i++) {
            uint32_t idx = i % POINTS_PER_THREAD;
            float value;
            uint64_t ts;
            uint8_t quality;
            
            // 混合读写操作
            if (i % 4 == 0) {
                // 写操作
                pool->setYCByIndex(idx, static_cast<float>(i), getCurrentTimestamp(), 0);
            } else {
                // 读操作
                pool->getYCByIndex(idx, value, ts, quality);
            }
            localOps++;
        }
        
        totalOps.fetch_add(localOps, std::memory_order_relaxed);
    };
    
    // 创建线程
    for (uint32_t t = 0; t < CONCURRENT_THREADS; t++) {
        threads.emplace_back(workerFunc, t);
    }
    
    // 开始计时
    Timer timer;
    start.store(true, std::memory_order_release);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    double seconds = timer.elapsedNs() / 1e9;
    uint64_t ops = totalOps.load();
    
    char buf[64];
    snprintf(buf, sizeof(buf), "并发混合读写 (%u 线程)", CONCURRENT_THREADS);
    printThroughput(buf, ops, seconds);
    printResult("平均单操作延迟", (seconds * 1e9) / ops, "ns");
}

void benchmarkEventPubSub(DataPoolClient* client) {
    std::cout << "\n=== 事件发布订阅延迟测试 ===\n";
    
    IPCEventCenter* eventCenter = client->getEventCenter();
    
    // 订阅者
    uint32_t subscriberId = client->subscribe([](const Event& /* e */) {
        // 空回调，仅测量传递延迟
    });
    
    if (subscriberId == INVALID_INDEX) {
        std::cout << "  跳过: 订阅失败\n";
        return;
    }
    
    // 预热
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        Event e;
        e.key = makeKey(500, i);
        e.pointType = PointType::YX;
        eventCenter->publish(e);
    }
    
    // 发布延迟测试
    Timer timer;
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        Event e;
        e.key = makeKey(500, i);
        e.pointType = PointType::YX;
        eventCenter->publish(e);
    }
    double publishNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("事件发布延迟", publishNs, "ns/event");
    
    // 发布吞吐量
    timer.reset();
    const uint32_t BATCH_EVENTS = 10000;
    for (uint32_t i = 0; i < BATCH_EVENTS; i++) {
        Event e;
        e.key = makeKey(500, i);
        e.pointType = PointType::YX;
        eventCenter->publish(e);
    }
    printThroughput("事件发布吞吐量", BATCH_EVENTS, timer.elapsedNs() / 1e9);
    
    client->unsubscribe(subscriberId);
}

void benchmarkHeartbeatOverhead(DataPoolClient* client) {
    std::cout << "\n=== 心跳与健康监控开销测试 ===\n";
    
    // 测试手动心跳更新延迟
    Timer timer;
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        client->updateHeartbeat();
    }
    double heartbeatNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("手动心跳更新延迟", heartbeatNs, "ns");
    
    // 启动心跳线程测试
    client->startHeartbeat(100);  // 100ms 间隔
    
    // 在心跳运行时测试操作延迟
    uint64_t key = makeKey(600, 1);
    uint32_t idx;
    client->registerPoint(key, PointType::YC, idx);
    
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        float value;
        uint8_t quality;
        client->getYCByIndex(idx, value, quality);
    }
    double readWithHeartbeatNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("心跳运行时读取延迟", readWithHeartbeatNs, "ns");
    
    // 启动健康监控测试
    uint32_t callbackCount = 0;
    client->startHealthMonitor(500, [&callbackCount](uint32_t, ProcessHealth, ProcessHealth) {
        callbackCount++;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    client->stopHealthMonitor();
    client->stopHeartbeat();
    
    printResult("健康监控回调次数 (1秒)", callbackCount, "次");
}

void benchmarkMemoryUsage(DataPoolClient* client) {
    std::cout << "\n=== 内存占用测试 ===\n";
    
    SharedDataPool* pool = client->getDataPool();
    const ShmHeader* header = pool->getHeader();
    
    // 计算内存使用
    size_t yxSize = header->yxCount * YXDataLayout::BYTES_PER_POINT;
    size_t ycSize = header->ycCount * YCDataLayout::BYTES_PER_POINT;
    size_t dzSize = header->dzCount * YCDataLayout::BYTES_PER_POINT;
    size_t ykSize = header->ykCount * YXDataLayout::BYTES_PER_POINT;
    
    size_t dataSize = yxSize + ycSize + dzSize + ykSize;
    size_t headerSize = sizeof(ShmHeader);
    
    printResult("YX 数据区大小", yxSize / 1024.0, "KB");
    printResult("YC 数据区大小", ycSize / 1024.0, "KB");
    printResult("DZ 数据区大小", dzSize / 1024.0, "KB");
    printResult("YK 数据区大小", ykSize / 1024.0, "KB");
    printResult("总数据区大小", dataSize / 1024.0, "KB");
    printResult("头部大小", headerSize, "bytes");
    printResult("每点平均内存", static_cast<double>(dataSize) / (header->yxCount + header->ycCount + header->dzCount + header->ykCount), "bytes/point");
    
    // 测试大数据池
    std::cout << "\n--- 大规模数据池内存估算 ---\n";
    uint32_t largeYxCount = 500000;
    uint32_t largeYcCount = 500000;
    uint32_t largeDzCount = 100000;
    uint32_t largeYkCount = 100000;
    
    size_t largeYxSize = largeYxCount * YXDataLayout::BYTES_PER_POINT;
    size_t largeYcSize = largeYcCount * YCDataLayout::BYTES_PER_POINT;
    size_t largeDzSize = largeDzCount * YCDataLayout::BYTES_PER_POINT;
    size_t largeYkSize = largeYkCount * YXDataLayout::BYTES_PER_POINT;
    size_t largeTotal = largeYxSize + largeYcSize + largeDzSize + largeYkSize;
    
    std::cout << "  估算配置: YX=" << largeYxCount << ", YC=" << largeYcCount 
              << ", DZ=" << largeDzCount << ", YK=" << largeYkCount << "\n";
    printResult("估算总内存", largeTotal / (1024.0 * 1024.0), "MB");
    printResult("估算每点内存", static_cast<double>(largeTotal) / (largeYxCount + largeYcCount + largeDzCount + largeYkCount), "bytes/point");
}

void benchmarkIndexLookup(DataPoolClient* client) {
    std::cout << "\n=== 索引查找性能测试 ===\n";
    
    SharedDataPool* pool = client->getDataPool();
    
    // 注册大量点位
    const uint32_t POINT_COUNT = 10000;
    for (uint32_t i = 0; i < POINT_COUNT; i++) {
        uint64_t key = makeKey(700, i);
        uint32_t idx;
        pool->registerKey(key, PointType::YC, idx);
    }
    
    // 测试随机查找
    uint64_t searchKey = makeKey(700, POINT_COUNT / 2);
    
    // 预热
    PointType type;
    uint32_t idx;
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        pool->findKey(searchKey, type, idx);
    }
    
    Timer timer;
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        pool->findKey(searchKey, type, idx);
    }
    double lookupNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("索引查找延迟", lookupNs, "ns");
    
    // 测试不存在的 key 查找
    uint64_t missingKey = makeKey(999, 99999);
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        pool->findKey(missingKey, type, idx);
    }
    double missingLookupNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("索引查找延迟 (不存在)", missingLookupNs, "ns");
}

void benchmarkProcessManagement(DataPoolClient* /* client */) {
    std::cout << "\n=== 进程管理性能测试 ===\n";
    
    // 测试进程注册延迟
    cleanupTestShm();
    SharedDataPool* pool = SharedDataPool::create(TEST_POOL_NAME, 1000, 1000, 100, 100);
    
    Timer timer;
    uint32_t processId;
    for (uint32_t i = 0; i < 100; i++) {
        pool->registerProcess("bench_process", processId);
    }
    double regNs = timer.elapsedNs() / 100;
    printResult("进程注册延迟", regNs, "ns");
    
    // 测试健康检查延迟
    timer.reset();
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++) {
        pool->checkProcessHealth(0);
    }
    double healthNs = timer.elapsedNs() / BENCH_ITERATIONS;
    printResult("进程健康检查延迟", healthNs, "ns");
    
    // 测试获取活跃进程列表
    timer.reset();
    for (uint32_t i = 0; i < 10000; i++) {
        uint32_t ids[32];
        pool->getActiveProcessList(ids, 32);
    }
    double listNs = timer.elapsedNs() / 10000;
    printResult("获取活跃进程列表延迟", listNs, "ns");
    
    pool->destroy();
    delete pool;
}

// ========== 主函数 ==========

int main() {
    std::cout << "========================================\n";
    std::cout << "IPCSharedDataPool 性能基准测试\n";
    std::cout << "========================================\n";
    std::cout << "迭代次数: " << BENCH_ITERATIONS << "\n";
    std::cout << "预热迭代: " << WARMUP_ITERATIONS << "\n";
    std::cout << "----------------------------------------\n";
    
    cleanupTestShm();
    
    // 创建客户端
    DataPoolClient::Config config;
    config.poolName = TEST_POOL_NAME;
    config.eventName = TEST_EVENT_NAME;
    config.soeName = TEST_SOE_NAME;
    config.processName = "benchmark";
    config.yxCount = 10000;
    config.ycCount = 10000;
    config.dzCount = 1000;
    config.ykCount = 1000;
    config.eventCapacity = 50000;
    config.create = true;
    config.enablePersistence = false;
    config.enableSOE = false;
    config.enableVoting = false;
    config.enableIEC61850 = false;
    
    DataPoolClient* client = DataPoolClient::init(config);
    if (!client) {
        std::cerr << "初始化客户端失败!\n";
        return 1;
    }
    
    // 运行基准测试
    benchmarkSingleRead(client);
    benchmarkSingleWrite(client);
    benchmarkBatchOperations(client);
    benchmarkConcurrentAccess(client);
    benchmarkEventPubSub(client);
    benchmarkIndexLookup(client);
    benchmarkProcessManagement(client);
    benchmarkHeartbeatOverhead(client);
    benchmarkMemoryUsage(client);
    
    // 清理
    client->shutdown();
    delete client;
    cleanupTestShm();
    
    std::cout << "\n========================================\n";
    std::cout << "基准测试完成\n";
    std::cout << "========================================\n";
    
    return 0;
}
