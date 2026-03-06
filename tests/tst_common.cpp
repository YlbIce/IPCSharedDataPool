/**
 * @file tst_common.cpp
 * @brief 通用定义测试
 */

#include "../include/Common.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

using namespace IPC;

// 测试结果统计
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

// ========== 测试用例 ==========

TEST(makeKey) {
    uint64_t key = makeKey(10000, 1);
    ASSERT_EQ(getKeyAddr(key), 10000);
    ASSERT_EQ(getKeyId(key), 1);
    
    // 测试边界值
    key = makeKey(0, 0);
    ASSERT_EQ(getKeyAddr(key), 0);
    ASSERT_EQ(getKeyId(key), 0);
    
    key = makeKey(0x7FFFFFFF, 0xFFFFFFFF);
    ASSERT_EQ(getKeyAddr(key), 0x7FFFFFFF);
    ASSERT_EQ(static_cast<uint32_t>(getKeyId(key)), 0xFFFFFFFF);
}

TEST(timestamp) {
    uint64_t t1 = getCurrentTimestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint64_t t2 = getCurrentTimestamp();
    
    ASSERT_TRUE(t2 > t1);
    ASSERT_TRUE(t2 - t1 >= 100);
    ASSERT_TRUE(t2 - t1 < 200);  // 允许一定误差
}

TEST(checksum) {
    const char* data = "Hello, World!";
    uint32_t sum1 = calculateChecksum(data, std::strlen(data));
    uint32_t sum2 = calculateChecksum(data, std::strlen(data));
    
    ASSERT_EQ(sum1, sum2);
    
    // 修改数据后校验和应该不同
    char data2[20];
    std::strcpy(data2, data);
    data2[0] = 'h';
    uint32_t sum3 = calculateChecksum(data2, std::strlen(data2));
    ASSERT_TRUE(sum1 != sum3);
}

TEST(event_size) {
    ASSERT_TRUE(sizeof(Event) == 64);
    
    Event e;
    e.key = makeKey(10000, 1);
    e.addr = 10000;
    e.id = 1;
    e.pointType = PointType::YC;
    e.oldValue.floatValue = 0.0f;
    e.newValue.floatValue = 23.5f;
    e.timestamp = getCurrentTimestamp();
    e.quality = 0;
    e.isCritical = 1;
    e.sourcePid = 1234;
    std::strncpy(e.source, "Test", sizeof(e.source) - 1);
    
    // 验证数据正确性
    ASSERT_EQ(e.key, makeKey(10000, 1));
    ASSERT_EQ(e.newValue.floatValue, 23.5f);
}

TEST(process_info) {
    ProcessInfo info;
    info.pid = getpid();
    info.setName("TestProcess");
    info.lastHeartbeat = getCurrentTimestamp();
    info.active = true;
    
    ASSERT_EQ(info.pid, getpid());
    ASSERT_TRUE(std::strcmp(info.name, "TestProcess") == 0);
    ASSERT_TRUE(info.active);
}

TEST(point_type) {
    // 验证枚举值
    ASSERT_TRUE(static_cast<int>(PointType::YX) == 0);
    ASSERT_TRUE(static_cast<int>(PointType::YC) == 1);
    ASSERT_TRUE(static_cast<int>(PointType::DZ) == 2);
    ASSERT_TRUE(static_cast<int>(PointType::YK) == 3);
}

TEST(data_layout) {
    // 验证数据布局大小
    ASSERT_EQ(YXDataLayout::BYTES_PER_POINT, 10);  // 1 + 8 + 1
    ASSERT_EQ(YCDataLayout::BYTES_PER_POINT, 13);  // 4 + 8 + 1
    ASSERT_EQ(DZDataLayout::BYTES_PER_POINT, 13);
    ASSERT_EQ(YKDataLayout::BYTES_PER_POINT, 10);
}

TEST(result_codes) {
    ASSERT_EQ(static_cast<int>(Result::OK), 0);
    ASSERT_TRUE(static_cast<int>(Result::ERROR) < 0);
    ASSERT_TRUE(static_cast<int>(Result::INVALID_PARAM) < 0);
}

// ========== 多线程测试 ==========

TEST(multithread_key) {
    const int THREAD_COUNT = 8;
    const int OPERATIONS = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<int> errors(0);
    
    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPERATIONS; i++) {
                int addr = t * 10000 + i;
                int id = i;
                uint64_t key = makeKey(addr, id);
                
                if (getKeyAddr(key) != addr || getKeyId(key) != id) {
                    errors++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(errors.load(), 0);
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  IPC Common Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    // 运行所有测试
    RUN_TEST(makeKey);
    RUN_TEST(timestamp);
    RUN_TEST(checksum);
    RUN_TEST(event_size);
    RUN_TEST(process_info);
    RUN_TEST(point_type);
    RUN_TEST(data_layout);
    RUN_TEST(result_codes);
    RUN_TEST(multithread_key);
    
    // 输出结果
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return s_failed > 0 ? 1 : 0;
}
