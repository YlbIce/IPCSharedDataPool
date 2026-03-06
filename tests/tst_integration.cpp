/**
 * @file tst_integration.cpp
 * @brief 集成测试
 */

#include "../include/DataPoolClient.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

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

static void cleanup() {
    shm_unlink("/ipc_data_pool");
    shm_unlink("/ipc_events");
}

// ========== 测试用例 ==========

TEST(client_create_shutdown) {
    cleanup();
    
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "test_process";
    config.yxCount = 100;
    config.ycCount = 100;
    config.dzCount = 50;
    config.ykCount = 50;
    config.eventCapacity = 1000;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    ASSERT_TRUE(client != nullptr);
    ASSERT_TRUE(client->isValid());
    ASSERT_TRUE(client->isCreator());
    ASSERT_TRUE(client->getProcessId() != INVALID_INDEX);
    
    client->shutdown();
    delete client;
}

TEST(data_operations) {
    cleanup();
    
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "data_test";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    ASSERT_TRUE(client != nullptr);
    
    // 注册点位
    uint32_t idx1, idx2;
    ASSERT_TRUE(client->registerPoint(makeKey(1, 1), PointType::YX, idx1));
    ASSERT_TRUE(client->registerPoint(makeKey(2, 1), PointType::YC, idx2));
    
    // YX 操作
    ASSERT_TRUE(client->setYX(makeKey(1, 1), 1));
    uint8_t yxValue, quality;
    ASSERT_TRUE(client->getYX(makeKey(1, 1), yxValue, quality));
    ASSERT_EQ(yxValue, 1u);
    
    // YC 操作
    ASSERT_TRUE(client->setYC(makeKey(2, 1), 23.5f));
    float ycValue;
    ASSERT_TRUE(client->getYC(makeKey(2, 1), ycValue, quality));
    ASSERT_TRUE(ycValue > 23.0f && ycValue < 24.0f);
    
    client->shutdown();
    delete client;
}

TEST(event_operations) {
    cleanup();
    
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "event_test";
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    ASSERT_TRUE(client != nullptr);
    
    std::atomic<int> eventCount(0);
    
    // 订阅
    uint32_t subId = client->subscribe([&](const Event& e) {
        eventCount++;
    });
    ASSERT_TRUE(subId != INVALID_INDEX);
    
    // 发布事件
    ASSERT_TRUE(client->publishEvent(makeKey(1, 1), PointType::YX, 
                                      uint32_t(0), uint32_t(1)));
    
    // 处理事件
    uint32_t processed = client->processEvents(subId, 0);
    ASSERT_EQ(processed, 1u);
    ASSERT_EQ(eventCount.load(), 1);
    
    // 取消订阅
    ASSERT_TRUE(client->unsubscribe(subId));
    
    client->shutdown();
    delete client;
}

TEST(set_with_event) {
    cleanup();
    
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "auto_event_test";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    ASSERT_TRUE(client != nullptr);
    
    // 注册点位
    uint32_t idx;
    client->registerPoint(makeKey(1, 1), PointType::YX, idx);
    client->registerPoint(makeKey(2, 1), PointType::YC, idx);
    
    std::atomic<int> eventCount(0);
    uint32_t subId = client->subscribe([&](const Event& e) {
        eventCount++;
    });
    
    // 设置值并发布事件
    ASSERT_TRUE(client->setYXWithEvent(makeKey(1, 1), 1));
    ASSERT_TRUE(client->setYCWithEvent(makeKey(2, 1), 45.6f));
    
    client->processEvents(subId, 0);
    ASSERT_EQ(eventCount.load(), 2);
    
    client->shutdown();
    delete client;
}

TEST(multi_client) {
    cleanup();
    
    // 创建服务器客户端
    DataPoolClient::Config serverConfig;
    serverConfig.poolName = "/ipc_data_pool";
    serverConfig.eventName = "/ipc_events";
    serverConfig.processName = "server";
    serverConfig.yxCount = 1000;
    serverConfig.ycCount = 1000;
    serverConfig.create = true;
    
    DataPoolClient* server = DataPoolClient::init(serverConfig);
    ASSERT_TRUE(server != nullptr);
    
    // 注册点位
    for (int i = 0; i < 100; i++) {
        uint32_t idx;
        server->registerPoint(makeKey(1, i), PointType::YX, idx);
        server->registerPoint(makeKey(2, i), PointType::YC, idx);
    }
    
    // 创建多个客户端
    std::vector<DataPoolClient*> clients;
    for (int i = 0; i < 3; i++) {
        DataPoolClient::Config clientConfig;
        clientConfig.poolName = "/ipc_data_pool";
        clientConfig.eventName = "/ipc_events";
        clientConfig.processName = "client_" + std::to_string(i);
        clientConfig.create = false;
        
        DataPoolClient* client = DataPoolClient::init(clientConfig);
        ASSERT_TRUE(client != nullptr);
        clients.push_back(client);
    }
    
    // 服务器写入数据
    for (int i = 0; i < 10; i++) {
        server->setYXByIndex(i, i % 2);
        server->setYCByIndex(i, static_cast<float>(i) * 10);
    }
    
    // 客户端读取数据
    for (auto* client : clients) {
        for (int i = 0; i < 10; i++) {
            uint8_t yxValue, quality;
            float ycValue;
            ASSERT_TRUE(client->getYXByIndex(i, yxValue, quality));
            ASSERT_TRUE(client->getYCByIndex(i, ycValue, quality));
        }
    }
    
    // 清理
    for (auto* client : clients) {
        client->shutdown();
        delete client;
    }
    
    server->shutdown();
    delete server;
}

TEST(concurrent_access) {
    cleanup();
    
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "concurrent_test";
    config.yxCount = 10000;
    config.ycCount = 10000;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    ASSERT_TRUE(client != nullptr);
    
    // 注册点位
    for (int i = 0; i < 1000; i++) {
        uint32_t idx;
        client->registerPoint(makeKey(1, i), PointType::YX, idx);
        client->registerPoint(makeKey(2, i), PointType::YC, idx);
    }
    
    std::atomic<int> errors(0);
    std::atomic<int> ops(0);
    
    // 多线程写入
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; t++) {
        writers.emplace_back([&, t]() {
            for (int i = 0; i < 500; i++) {
                uint32_t idx = t * 125 + i / 4;
                if (idx >= 1000) continue;
                
                if (!client->setYXByIndex(idx, i % 2)) {
                    errors++;
                }
                if (!client->setYCByIndex(idx, static_cast<float>(i))) {
                    errors++;
                }
                ops += 2;
            }
        });
    }
    
    // 多线程读取
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) {
        readers.emplace_back([&]() {
            for (int i = 0; i < 500; i++) {
                uint8_t yx;
                float yc;
                uint8_t q;
                client->getYXByIndex(i % 1000, yx, q);
                client->getYCByIndex(i % 1000, yc, q);
                ops += 2;
            }
        });
    }
    
    for (auto& t : writers) t.join();
    for (auto& t : readers) t.join();
    
    ASSERT_EQ(errors.load(), 0);
    
    client->shutdown();
    delete client;
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  Integration Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RUN_TEST(client_create_shutdown);
    RUN_TEST(data_operations);
    RUN_TEST(event_operations);
    RUN_TEST(set_with_event);
    RUN_TEST(multi_client);
    RUN_TEST(concurrent_access);
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    cleanup();
    
    return s_failed > 0 ? 1 : 0;
}
