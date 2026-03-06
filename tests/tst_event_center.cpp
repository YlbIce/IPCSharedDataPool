/**
 * @file tst_event_center.cpp
 * @brief 事件中心测试
 */

#include "../include/IPCEventCenter.h"
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

static const char* TEST_SHM_NAME = "/ipc_test_events";

static void cleanupTestShm() {
    shm_unlink(TEST_SHM_NAME);
}

// ========== 测试用例 ==========

TEST(create_destroy) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 1000);
    ASSERT_TRUE(center != nullptr);
    ASSERT_TRUE(center->isValid());
    ASSERT_EQ(center->getEventCapacity(), 1000u);
    
    center->destroy();
    delete center;
}

TEST(connect_disconnect) {
    cleanupTestShm();
    
    IPCEventCenter* center1 = IPCEventCenter::create(TEST_SHM_NAME, 1000);
    ASSERT_TRUE(center1 != nullptr);
    
    IPCEventCenter* center2 = IPCEventCenter::connect(TEST_SHM_NAME);
    ASSERT_TRUE(center2 != nullptr);
    ASSERT_TRUE(center2->isValid());
    ASSERT_EQ(center2->getEventCapacity(), 1000u);
    
    center2->disconnect();
    delete center2;
    
    center1->destroy();
    delete center1;
}

TEST(publish_poll) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 100);
    ASSERT_TRUE(center != nullptr);
    
    // 订阅
    uint32_t subId;
    ASSERT_EQ(center->subscribe([](const Event&) {}, subId), Result::OK);
    
    // 发布事件
    Event event;
    event.key = makeKey(1, 1);
    event.pointType = PointType::YX;
    event.newValue.intValue = 42;
    event.timestamp = getCurrentTimestamp();
    
    ASSERT_EQ(center->publish(event), Result::OK);
    ASSERT_EQ(center->getTotalEvents(), 1u);
    
    // 拉取事件
    Event received;
    ASSERT_EQ(center->poll(subId, received), Result::OK);
    ASSERT_EQ(received.key, event.key);
    ASSERT_EQ(received.newValue.intValue, 42u);
    
    // 再次拉取应该无数据
    ASSERT_EQ(center->poll(subId, received), Result::NOT_FOUND);
    
    center->destroy();
    delete center;
}

TEST(multi_subscriber) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 100);
    ASSERT_TRUE(center != nullptr);
    
    // 多个订阅者
    uint32_t subId1, subId2, subId3;
    ASSERT_EQ(center->subscribe([](const Event&) {}, subId1), Result::OK);
    ASSERT_EQ(center->subscribe([](const Event&) {}, subId2), Result::OK);
    ASSERT_EQ(center->subscribe([](const Event&) {}, subId3), Result::OK);
    
    ASSERT_EQ(center->getSubscriberCount(), 3u);
    
    // 发布事件
    Event event;
    event.key = makeKey(1, 1);
    event.pointType = PointType::YC;
    event.newValue.floatValue = 123.45f;
    
    ASSERT_EQ(center->publish(event), Result::OK);
    
    // 每个订阅者都应该能收到
    Event received;
    ASSERT_EQ(center->poll(subId1, received), Result::OK);
    ASSERT_EQ(center->poll(subId2, received), Result::OK);
    ASSERT_EQ(center->poll(subId3, received), Result::OK);
    
    // 取消订阅
    ASSERT_EQ(center->unsubscribe(subId2), Result::OK);
    ASSERT_EQ(center->getSubscriberCount(), 2u);
    
    center->destroy();
    delete center;
}

TEST(cross_process_events) {
    cleanupTestShm();
    
    // 进程1：创建
    IPCEventCenter* center1 = IPCEventCenter::create(TEST_SHM_NAME, 100);
    ASSERT_TRUE(center1 != nullptr);
    
    // 进程2：连接并先订阅
    IPCEventCenter* center2 = IPCEventCenter::connect(TEST_SHM_NAME);
    ASSERT_TRUE(center2 != nullptr);
    
    uint32_t subId;
    center2->subscribe([](const Event&) {}, subId);
    
    // 进程1 发布事件
    Event event;
    event.key = makeKey(1, 1);
    event.pointType = PointType::YX;
    event.newValue.intValue = 99;
    event.timestamp = getCurrentTimestamp();
    
    ASSERT_EQ(center1->publish(event), Result::OK);
    
    // 进程2 拉取事件
    Event received;
    ASSERT_EQ(center2->poll(subId, received), Result::OK);
    ASSERT_EQ(received.key, event.key);
    ASSERT_EQ(received.newValue.intValue, 99u);
    
    center2->disconnect();
    delete center2;
    
    center1->destroy();
    delete center1;
}

TEST(concurrent_publish) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 10000);
    ASSERT_TRUE(center != nullptr);
    
    uint32_t subId;
    center->subscribe([](const Event&) {}, subId);
    
    const int THREAD_COUNT = 4;
    const int EVENTS_PER_THREAD = 100;
    std::atomic<int> totalPublished(0);
    
    std::vector<std::thread> publishers;
    for (int t = 0; t < THREAD_COUNT; t++) {
        publishers.emplace_back([&, t]() {
            for (int i = 0; i < EVENTS_PER_THREAD; i++) {
                Event event;
                event.key = makeKey(t, i);
                event.pointType = PointType::YC;
                event.newValue.floatValue = static_cast<float>(i);
                
                if (center->publish(event) == Result::OK) {
                    totalPublished++;
                }
            }
        });
    }
    
    for (auto& t : publishers) {
        t.join();
    }
    
    ASSERT_EQ(totalPublished.load(), THREAD_COUNT * EVENTS_PER_THREAD);
    
    // 读取所有事件
    int readCount = 0;
    Event event;
    while (center->poll(subId, event) == Result::OK) {
        readCount++;
    }
    ASSERT_EQ(readCount, THREAD_COUNT * EVENTS_PER_THREAD);
    
    center->destroy();
    delete center;
}

TEST(callback_process) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 100);
    ASSERT_TRUE(center != nullptr);
    
    std::atomic<int> callbackCount(0);
    
    uint32_t subId;
    ASSERT_EQ(center->subscribe([&](const Event& e) {
        callbackCount++;
    }, subId), Result::OK);
    
    // 发布事件
    for (int i = 0; i < 10; i++) {
        Event event;
        event.key = i;
        center->publish(event);
    }
    
    // 处理事件
    uint32_t processed = center->process(subId, 0);
    ASSERT_EQ(processed, 10u);
    ASSERT_EQ(callbackCount.load(), 10);
    
    center->destroy();
    delete center;
}

TEST(publish_data_change) {
    cleanupTestShm();
    
    IPCEventCenter* center = IPCEventCenter::create(TEST_SHM_NAME, 100);
    ASSERT_TRUE(center != nullptr);
    
    uint32_t subId;
    center->subscribe([](const Event&) {}, subId);
    
    // 发布整数变更
    ASSERT_EQ(center->publishDataChange(makeKey(1, 1), PointType::YX, 
                                         uint32_t(0), uint32_t(1), "test"), Result::OK);
    
    Event event;
    ASSERT_EQ(center->poll(subId, event), Result::OK);
    ASSERT_EQ(event.oldValue.intValue, 0u);
    ASSERT_EQ(event.newValue.intValue, 1u);
    ASSERT_EQ(std::string(event.source), "test");
    
    // 发布浮点变更
    ASSERT_EQ(center->publishDataChange(makeKey(2, 1), PointType::YC,
                                         10.5f, 20.5f, "sensor"), Result::OK);
    
    ASSERT_EQ(center->poll(subId, event), Result::OK);
    ASSERT_TRUE(event.oldValue.floatValue > 10.0f && event.oldValue.floatValue < 11.0f);
    ASSERT_TRUE(event.newValue.floatValue > 20.0f && event.newValue.floatValue < 21.0f);
    
    center->destroy();
    delete center;
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  IPCEventCenter Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RUN_TEST(create_destroy);
    RUN_TEST(connect_disconnect);
    RUN_TEST(publish_poll);
    RUN_TEST(multi_subscriber);
    RUN_TEST(cross_process_events);
    RUN_TEST(concurrent_publish);
    RUN_TEST(callback_process);
    RUN_TEST(publish_data_change);
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    cleanupTestShm();
    
    return s_failed > 0 ? 1 : 0;
}
