/**
 * @file tst_new_features.cpp
 * @brief 测试新功能：统计、健康检查、快照持久化
 */

#include <iostream>
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include "../include/DataPoolClient.h"

using namespace IPC;

bool test_stats() {
    std::cout << "Running stats... ";
    
    // 清理
    shm_unlink("/test_stats_pool");
    shm_unlink("/test_stats_events");
    
    DataPoolClient::Config config;
    config.poolName = "/test_stats_pool";
    config.eventName = "/test_stats_events";
    config.processName = "stats_test";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    assert(client != nullptr);
    
    // 初始统计应为空
    DataPoolStats stats = client->getStats();
    assert(stats.totalWrites == 0);
    assert(stats.totalReads == 0);
    
    // 注册并写入数据
    uint32_t idx;
    client->registerPoint(makeKey(1, 1), PointType::YX, idx);
    client->setYX(makeKey(1, 1), 1);
    
    stats = client->getStats();
    assert(stats.totalWrites == 1);
    assert(stats.yxWrites == 1);
    
    // 读取数据
    uint8_t value, quality;
    client->getYX(makeKey(1, 1), value, quality);
    
    stats = client->getStats();
    assert(stats.totalReads == 1);
    
    // 写入YC
    client->registerPoint(makeKey(2, 1), PointType::YC, idx);
    client->setYC(makeKey(2, 1), 3.14f);
    
    stats = client->getStats();
    assert(stats.totalWrites == 2);
    assert(stats.ycWrites == 1);
    
    // 重置统计
    client->resetStats();
    stats = client->getStats();
    assert(stats.totalWrites == 0);
    assert(stats.totalReads == 0);
    
    client->shutdown();
    delete client;
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_health_check() {
    std::cout << "Running health_check... ";
    
    shm_unlink("/test_health_pool");
    shm_unlink("/test_health_events");
    
    DataPoolClient::Config config;
    config.poolName = "/test_health_pool";
    config.eventName = "/test_health_events";
    config.processName = "health_test";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    assert(client != nullptr);
    
    // 获取自己的进程ID
    uint32_t processIds[32];
    uint32_t count = client->getActiveProcessList(processIds, 32);
    assert(count == 1);
    
    // 检查健康状态
    ProcessHealth health = client->checkProcessHealth(processIds[0]);
    assert(health == ProcessHealth::HEALTHY);
    
    // 更新心跳
    client->updateHeartbeat();
    health = client->checkProcessHealth(processIds[0]);
    assert(health == ProcessHealth::HEALTHY);
    
    // 清理死亡进程（应该返回0，因为没有死亡进程）
    uint32_t cleaned = client->cleanupDeadProcesses();
    assert(cleaned == 0);
    
    client->shutdown();
    delete client;
    
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_snapshot() {
    std::cout << "Running snapshot... ";
    
    const char* snapshotFile = "/tmp/test_snapshot.bin";
    
    shm_unlink("/test_snap_pool");
    shm_unlink("/test_snap_events");
    
    // 创建数据池并写入数据
    DataPoolClient::Config config;
    config.poolName = "/test_snap_pool";
    config.eventName = "/test_snap_events";
    config.processName = "snap_test";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;
    
    DataPoolClient* client = DataPoolClient::init(config);
    assert(client != nullptr);
    
    // 注册并写入数据
    uint32_t idx;
    client->registerPoint(makeKey(1, 1), PointType::YX, idx);
    client->setYX(makeKey(1, 1), 42);
    
    client->registerPoint(makeKey(2, 1), PointType::YC, idx);
    client->setYC(makeKey(2, 1), 3.14159f);
    
    // 保存快照
    bool saved = client->saveSnapshot(snapshotFile);
    assert(saved);
    
    // 验证快照
    bool valid = client->validateSnapshot(snapshotFile);
    assert(valid);
    
    // 修改数据
    client->setYX(makeKey(1, 1), 0);
    client->setYC(makeKey(2, 1), 0.0f);
    
    // 加载快照
    bool loaded = client->loadSnapshot(snapshotFile);
    assert(loaded);
    
    // 验证数据已恢复
    uint8_t yxValue;
    uint8_t quality;
    client->getYX(makeKey(1, 1), yxValue, quality);
    assert(yxValue == 42);
    
    float ycValue;
    client->getYC(makeKey(2, 1), ycValue, quality);
    assert(ycValue > 3.14f && ycValue < 3.15f);
    
    client->shutdown();
    delete client;
    
    // 清理
    std::remove(snapshotFile);
    
    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  New Features Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    int passed = 0, failed = 0;
    
    if (test_stats()) passed++; else failed++;
    if (test_health_check()) passed++; else failed++;
    if (test_snapshot()) passed++; else failed++;
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
