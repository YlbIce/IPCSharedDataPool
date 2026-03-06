/**
 * @file tst_shm_pool.cpp
 * @brief 共享数据池测试
 */

#include "../include/SharedDataPool.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

using namespace IPC;

static int s_passed = 0;
static int s_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED" << std::endl; \
        s_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << std::endl; \
        s_failed++; \
    } catch (...) { \
        std::cout << "FAILED: unknown error" << std::endl; \
        s_failed++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

static const char* TEST_SHM_NAME = "/ipc_test_pool";

// 清理测试共享内存
static void cleanupTestShm() {
    shm_unlink(TEST_SHM_NAME);
}

// ========== 测试用例 ==========

TEST(create_destroy) {
    cleanupTestShm();
    
    SharedDataPool* pool = SharedDataPool::create(TEST_SHM_NAME, 100, 100, 50, 20);
    ASSERT_TRUE(pool != nullptr);
    ASSERT_TRUE(pool->isValid());
    ASSERT_EQ(pool->getYXCount(), 100u);
    ASSERT_EQ(pool->getYCCount(), 100u);
    ASSERT_EQ(pool->getDZCount(), 50u);
    ASSERT_EQ(pool->getYKCount(), 20u);
    
    pool->destroy();
    delete pool;
}

TEST(connect_disconnect) {
    cleanupTestShm();
    
    // 创建
    SharedDataPool* pool1 = SharedDataPool::create(TEST_SHM_NAME, 100, 100, 50, 20);
    ASSERT_TRUE(pool1 != nullptr);
    
    // 连接
    SharedDataPool* pool2 = SharedDataPool::connect(TEST_SHM_NAME);
    ASSERT_TRUE(pool2 != nullptr);
    ASSERT_TRUE(pool2->isValid());
    ASSERT_EQ(pool2->getYXCount(), 100u);
    
    // 断开连接
    pool2->disconnect();
    delete pool2;
    
    // 销毁
    pool1->destroy();
    delete pool1;
}

TEST(yx_operations) {
    cleanupTestShm();
    
    SharedDataPool* pool = SharedDataPool::create(TEST_SHM_NAME, 100, 0, 0, 0);
    ASSERT_TRUE(pool != nullptr);
    
    // 注册点位
    uint32_t idx1, idx2;
    ASSERT_EQ(pool->registerKey(makeKey(1, 1), PointType::YX, idx1), Result::OK);
    ASSERT_EQ(idx1, 0u);
    ASSERT_EQ(pool->registerKey(makeKey(1, 2), PointType::YX, idx2), Result::OK);
    ASSERT_EQ(idx2, 1u);
    
    // 通过索引设置
    ASSERT_EQ(pool->setYXByIndex(0, 1, 1000, 0), Result::OK);
    ASSERT_EQ(pool->setYXByIndex(1, 0, 2000, 1), Result::OK);
    
    // 通过索引读取
    uint8_t value;
    uint64_t timestamp;
    uint8_t quality;
    ASSERT_EQ(pool->getYXByIndex(0, value, timestamp, quality), Result::OK);
    ASSERT_EQ(value, 1u);
    ASSERT_EQ(timestamp, 1000u);
    
    ASSERT_EQ(pool->getYXByIndex(1, value, timestamp, quality), Result::OK);
    ASSERT_EQ(value, 0u);
    ASSERT_EQ(timestamp, 2000u);
    ASSERT_EQ(quality, 1u);
    
    // 通过 key 操作
    ASSERT_EQ(pool->setYX(makeKey(1, 1), 1, 3000, 0), Result::OK);
    ASSERT_EQ(pool->getYX(makeKey(1, 1), value, timestamp, quality), Result::OK);
    ASSERT_EQ(value, 1u);
    ASSERT_EQ(timestamp, 3000u);
    
    // 不存在的 key
    ASSERT_EQ(pool->getYX(makeKey(99, 99), value, timestamp, quality), Result::NOT_FOUND);
    
    pool->destroy();
    delete pool;
}

TEST(yc_operations) {
    cleanupTestShm();
    
    SharedDataPool* pool = SharedDataPool::create(TEST_SHM_NAME, 0, 100, 0, 0);
    ASSERT_TRUE(pool != nullptr);
    
    // 注册点位
    uint32_t idx;
    ASSERT_EQ(pool->registerKey(makeKey(2, 1), PointType::YC, idx), Result::OK);
    
    // 设置浮点值
    float value = 23.5f;
    ASSERT_EQ(pool->setYCByIndex(0, value, 1000, 0), Result::OK);
    
    // 读取
    float readValue;
    uint64_t timestamp;
    uint8_t quality;
    ASSERT_EQ(pool->getYCByIndex(0, readValue, timestamp, quality), Result::OK);
    ASSERT_TRUE(readValue > 23.4f && readValue < 23.6f);
    ASSERT_EQ(timestamp, 1000u);
    
    // 通过 key 操作
    value = 45.8f;
    ASSERT_EQ(pool->setYC(makeKey(2, 1), value, 2000, 1), Result::OK);
    ASSERT_EQ(pool->getYC(makeKey(2, 1), readValue, timestamp, quality), Result::OK);
    ASSERT_TRUE(readValue > 45.7f && readValue < 45.9f);
    ASSERT_EQ(quality, 1u);
    
    pool->destroy();
    delete pool;
}

TEST(multi_process) {
    cleanupTestShm();
    
    // 创建
    SharedDataPool* pool = SharedDataPool::create(TEST_SHM_NAME, 1000, 1000, 0, 0);
    ASSERT_TRUE(pool != nullptr);
    
    // 注册一些点位
    for (int i = 0; i < 100; i++) {
        uint32_t idx;
        pool->registerKey(makeKey(1, i), PointType::YX, idx);
        pool->registerKey(makeKey(2, i), PointType::YC, idx);
    }
    
    // 多线程并发读写
    std::atomic<int> errors(0);
    std::atomic<int> ops(0);
    
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; t++) {
        writers.emplace_back([&, t]() {
            for (int i = 0; i < 100; i++) {
                uint32_t idx = t * 25 + i / 4;
                if (idx >= 100) continue;
                
                pool->setYXByIndex(idx, i % 2, getCurrentTimestamp(), 0);
                pool->setYCByIndex(idx, static_cast<float>(i), getCurrentTimestamp(), 0);
                ops++;
            }
        });
    }
    
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) {
        readers.emplace_back([&]() {
            for (int i = 0; i < 100; i++) {
                uint8_t v;
                uint64_t ts;
                uint8_t q;
                float fv;
                
                pool->getYXByIndex(i % 100, v, ts, q);
                pool->getYCByIndex(i % 100, fv, ts, q);
                ops++;
            }
        });
    }
    
    for (auto& t : writers) t.join();
    for (auto& t : readers) t.join();
    
    ASSERT_EQ(errors.load(), 0);
    
    pool->destroy();
    delete pool;
}

TEST(process_registration) {
    cleanupTestShm();
    
    SharedDataPool* pool = SharedDataPool::create(TEST_SHM_NAME, 10, 10, 10, 10);
    ASSERT_TRUE(pool != nullptr);
    
    // 注册进程
    uint32_t pid1, pid2;
    ASSERT_EQ(pool->registerProcess("TestProcess1", pid1), Result::OK);
    ASSERT_EQ(pool->registerProcess("TestProcess2", pid2), Result::OK);
    
    // 获取进程信息
    ProcessInfo info;
    ASSERT_EQ(pool->getProcessInfo(pid1, info), Result::OK);
    ASSERT_EQ(std::string(info.name), "TestProcess1");
    ASSERT_TRUE(info.active);
    
    // 更新心跳
    ASSERT_EQ(pool->updateHeartbeat(pid1), Result::OK);
    
    // 注销进程
    ASSERT_EQ(pool->unregisterProcess(pid1), Result::OK);
    ASSERT_EQ(pool->getProcessInfo(pid1, info), Result::OK);
    ASSERT_TRUE(!info.active);
    
    pool->destroy();
    delete pool;
}

TEST(cross_process_data) {
    cleanupTestShm();
    
    // 进程1：创建并写入
    SharedDataPool* pool1 = SharedDataPool::create(TEST_SHM_NAME, 100, 100, 0, 0);
    ASSERT_TRUE(pool1 != nullptr);
    
    uint32_t idx;
    pool1->registerKey(makeKey(1, 1), PointType::YX, idx);
    pool1->registerKey(makeKey(2, 1), PointType::YC, idx);
    
    pool1->setYXByIndex(0, 1, 1000, 0);
    pool1->setYCByIndex(0, 123.45f, 2000, 0);
    
    // 进程2：连接并读取
    SharedDataPool* pool2 = SharedDataPool::connect(TEST_SHM_NAME);
    ASSERT_TRUE(pool2 != nullptr);
    
    uint8_t yxValue;
    uint64_t ts;
    uint8_t q;
    ASSERT_EQ(pool2->getYXByIndex(0, yxValue, ts, q), Result::OK);
    ASSERT_EQ(yxValue, 1u);
    ASSERT_EQ(ts, 1000u);
    
    float ycValue;
    ASSERT_EQ(pool2->getYCByIndex(0, ycValue, ts, q), Result::OK);
    ASSERT_TRUE(ycValue > 123.0f && ycValue < 124.0f);
    
    // 进程2 修改数据
    pool2->setYXByIndex(0, 0, 3000, 1);
    
    // 进程1 验证修改
    ASSERT_EQ(pool1->getYXByIndex(0, yxValue, ts, q), Result::OK);
    ASSERT_EQ(yxValue, 0u);
    ASSERT_EQ(ts, 3000u);
    ASSERT_EQ(q, 1u);
    
    pool2->disconnect();
    delete pool2;
    
    pool1->destroy();
    delete pool1;
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  SharedDataPool Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RUN_TEST(create_destroy);
    RUN_TEST(connect_disconnect);
    RUN_TEST(yx_operations);
    RUN_TEST(yc_operations);
    RUN_TEST(multi_process);
    RUN_TEST(process_registration);
    RUN_TEST(cross_process_data);
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    cleanupTestShm();
    
    return s_failed > 0 ? 1 : 0;
}
