/**
 * @file tst_goose_adapter.cpp
 * @brief GOOSE 适配器单元测试
 */

#ifdef HAS_LIBIEC61850

#include <gtest/gtest.h>
#include "GooseAdapter.h"

namespace IPC {

/**
 * @brief GOOSE 适配器测试类
 */
class GooseAdapterTest : public ::testing::Test {
};

TEST_F(GooseAdapterTest, Initialization) {
    // 测试正常初始化
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(adapter->isInitialized());
    EXPECT_TRUE(adapter->isEnabled());
    EXPECT_EQ(adapter->getGoID(), "TestGooseID");
    EXPECT_EQ(adapter->getGoCBRef(), "IED/LLN0$GO$gcbEvents");

    adapter->destroy();
}

TEST_F(GooseAdapterTest, AddBooleanMember) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    // 添加布尔型成员
    GooseDataSetMember boolMember;
    boolMember.name = "status";
    boolMember.type = GooseDataType::BOOLEAN;
    boolMember.index = 0;

    int result = adapter->addDataSetMember(boolMember);
    EXPECT_GE(result, 0);
    EXPECT_EQ(adapter->getMemberCount(), 1);

    // 更新值
    MmsValue* value = MmsValue_newBoolean(true);
    adapter->updateMemberValue(0, value);

    adapter->destroy();
    MmsValue_delete(value);
}

TEST_F(GooseAdapterTest, AddIntegerMember) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    // 添加整型成员
    GooseDataSetMember intMember;
    intMember.name = "counter";
    intMember.type = GooseDataType::INTEGER;
    intMember.index = 1;

    int result = adapter->addDataSetMember(intMember);
    EXPECT_GE(result, 1);
    EXPECT_EQ(adapter->getMemberCount(), 2);

    // 更新值
    MmsValue* value = MmsValue_newInteger(42);
    adapter->updateMemberValue(1, value);

    adapter->destroy();
    MmsValue_delete(value);
}

TEST_F(GooseAdapterTest, Publish) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    // 添加成员并设置值
    GooseDataSetMember boolMember;
    boolMember.name = "status";
    boolMember.type = GooseDataType::BOOLEAN;
    boolMember.index = 0;

    adapter->addDataSetMember(boolMember);

    GooseDataSetMember intMember;
    intMember.name = "counter";
    intMember.type = GooseDataType::INTEGER;
    intMember.index = 1;

    adapter->addDataSetMember(intMember);

    MmsValue* boolValue = MmsValue_newBoolean(true);
    MmsValue* intValue = MmsValue_newInteger(10);

    adapter->updateMemberValue(0, boolValue);
    adapter->updateMemberValue(1, intValue);

    // 发布
    bool result = adapter->publish();
    EXPECT_TRUE(result);

    // 检查状态号和序列号
    uint32_t stNum = adapter->getStNum();
    uint32_t sqNum = adapter->getSqNum();
    EXPECT_GT(stNum, 0);
    EXPECT_GE(sqNum, 0);

    adapter->destroy();
    MmsValue_delete(boolValue);
    MmsValue_delete(intValue);
}

TEST_F(GooseAdapterTest, PublishCallback) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    bool callbackCalled = false;
    std::string capturedRef;

    // 设置回调
    adapter->setDataChangeCallback([&capturedRef, &callbackCalled](const std::string& ref) {
        capturedRef = ref;
        callbackCalled = true;
    });

    // 发布
    adapter->publish();
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(capturedRef, "IED/LLN0$GO$gcbEvents");

    adapter->destroy();
}

TEST_F(GooseAdapterTest, StNumHandling) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    GooseDataSetMember member;
    member.name = "test";
    member.type = GooseDataType::BOOLEAN;
    member.index = 0;
    adapter->addDataSetMember(member);

    MmsValue* value = MmsValue_newBoolean(true);
    adapter->updateMemberValue(0, value);

    // 检查初始状态号
    uint32_t initialStNum = adapter->getStNum();
    EXPECT_EQ(initialStNum, 1);

    // 发布，不增加 stNum
    bool result = adapter->publish(false);  // increaseStNum = false
    EXPECT_TRUE(result);

    uint32_t stNumAfter = adapter->getStNum();
    EXPECT_EQ(stNumAfter, 1);  // stNum 不应该变化

    // 手动增加 stNum
    uint64_t newStNum = adapter->increaseStNum();
    EXPECT_GT(newStNum, 1);
    EXPECT_EQ(adapter->getStNum(), static_cast<uint32_t>(newStNum));

    adapter->destroy();
    MmsValue_delete(value);
}

TEST_F(GooseAdapterTest, Reset) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 100;
    config.appId = 0x0001;
    memcpy(config.dstAddress, "\x01\x0C\xCD\x01\x00\x01", 6);
    config.useVlanTag = true;
    config.goID = "TestGooseID";
    config.goCBRef = "IED/LLN0$GO$gcbEvents";
    config.dataSetRef = "IED/LLN0$GO$gcbEvents";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    GooseAdapter* adapter = GooseAdapter::create(config);
    ASSERT_NE(adapter, nullptr);

    // 设置初始状态号
    adapter->setStNum(10);
    adapter->setSqNum(5);

    EXPECT_EQ(adapter->getStNum(), 10);
    EXPECT_EQ(adapter->getSqNum(), 5);

    // 重置
    adapter->reset();

    EXPECT_EQ(adapter->getStNum(), 1);
    EXPECT_EQ(adapter->getSqNum(), 0);

    adapter->destroy();
}

} // namespace IPC

#endif // HAS_LIBIEC61850
