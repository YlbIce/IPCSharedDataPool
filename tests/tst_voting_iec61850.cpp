#include <QtTest/QtTest>
#include <QtCore/QObject>
#include <sys/mman.h>
#include "VotingEngine.h"
#include "IEC61850Mapper.h"

using namespace IPC;

class TestVotingAndIEC61850 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    
    // 三取二表决测试
    void testVotingEngineCreation();
    void testVotingGroupAdd();
    void testVotingYX_Unanimous();
    void testVotingYX_Majority();
    void testVotingYX_Disagree();
    void testVotingYX_Insufficient();
    void testVotingYC_Unanimous();
    void testVotingYC_Deviation();
    void testQuickVoteYX();
    void testQuickVoteYC();
    void testDeviationAlarm();
    void testVotingStats();
    
    // IEC 61850映射测试
    void testIEC61850MapperCreation();
    void testAddDAMapping();
    void testFindMapping();
    void testAddLogicalNode();
    void testCreateDataSet();
    void testQualityConversion();
    void testExportMappings();
    
private:
    VotingEngine* m_votingEngine;
    IEC61850Mapper* m_iecMapper;
    bool m_alarmTriggered;
    uint32_t m_alarmGroupId;
    DeviationLevel m_alarmLevel;
};

void TestVotingAndIEC61850::initTestCase() {
    m_votingEngine = nullptr;
    m_iecMapper = nullptr;
    m_alarmTriggered = false;
    
    // 清理可能存在的共享内存
    shm_unlink("/ipc_voting_test");
    shm_unlink("/ipc_iec61850_test");
}

void TestVotingAndIEC61850::cleanupTestCase() {
    if (m_votingEngine) {
        m_votingEngine->destroy();
        m_votingEngine = nullptr;
    }
    
    if (m_iecMapper) {
        m_iecMapper->destroy();
        m_iecMapper = nullptr;
    }
    
    // 清理共享内存
    shm_unlink("/ipc_voting_test");
    shm_unlink("/ipc_iec61850_test");
}

// ==================== 三取二表决测试 ====================

void TestVotingAndIEC61850::testVotingEngineCreation() {
    VotingEngine::ShmConfig config;
    config.shmName = "/ipc_voting_test";
    config.maxGroups = 100;
    config.create = true;
    
    m_votingEngine = VotingEngine::create(config);
    QVERIFY(m_votingEngine != nullptr);
    QCOMPARE(m_votingEngine->getVotingGroupCount(), 0u);
}

void TestVotingAndIEC61850::testVotingGroupAdd() {
    QVERIFY(m_votingEngine != nullptr);
    
    VotingConfig config;
    config.groupId = 1;
    strncpy(config.name, "Breaker1_Trip", sizeof(config.name) - 1);
    config.sourceKeyA = makeKey(1, 100);
    config.sourceKeyB = makeKey(2, 100);
    config.sourceKeyC = makeKey(3, 100);
    config.sourceType = 0; // YX
    config.votingStrategy = 0; // 严格三取二
    config.enableDeviation = 1;
    config.deviationCountLimit = 3;
    
    uint32_t id = m_votingEngine->addVotingGroup(config);
    QVERIFY(id != INVALID_INDEX);
    QCOMPARE(m_votingEngine->getVotingGroupCount(), 1u);
    
    // 验证配置
    VotingConfig retrieved;
    QVERIFY(m_votingEngine->getVotingGroupConfig(1, retrieved));
    QCOMPARE(retrieved.groupId, 1u);
    QCOMPARE(strcmp(retrieved.name, "Breaker1_Trip"), 0);
    
    // 添加第二个表决组
    config.groupId = 2;
    strncpy(config.name, "Voltage_A", sizeof(config.name) - 1);
    config.sourceType = 1; // YC
    config.deviationLimit = 0.5f;
    
    id = m_votingEngine->addVotingGroup(config);
    QVERIFY(id != INVALID_INDEX);
    QCOMPARE(m_votingEngine->getVotingGroupCount(), 2u);
}

void TestVotingAndIEC61850::testVotingYX_Unanimous() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 创建三个一致的YX源
    SourceData sources[3];
    for (int i = 0; i < 3; i++) {
        sources[i].yxValue = 1;
        sources[i].quality = 0;
        sources[i].status = static_cast<uint8_t>(SourceStatus::VALID);
        sources[i].timestamp = getCurrentTimestamp();
    }
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYX(1, sources, output));
    
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::UNANIMOUS);
    QCOMPARE(output.yxValue, static_cast<uint8_t>(1));
    QCOMPARE(output.validSourceCount, static_cast<uint8_t>(3));
    QCOMPARE(output.quality, static_cast<uint8_t>(0));
}

void TestVotingAndIEC61850::testVotingYX_Majority() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 创建三个源，两个一致
    SourceData sources[3];
    sources[0].yxValue = 1;
    sources[0].quality = 0;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[1].yxValue = 1;
    sources[1].quality = 0;
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[2].yxValue = 0;
    sources[2].quality = 0;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYX(1, sources, output));
    
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::MAJORITY);
    QCOMPARE(output.yxValue, static_cast<uint8_t>(1));
    QCOMPARE(output.validSourceCount, static_cast<uint8_t>(3));
}

void TestVotingAndIEC61850::testVotingYX_Disagree() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 创建三个源，各不相同
    SourceData sources[3];
    sources[0].yxValue = 0;
    sources[0].quality = 0;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[1].yxValue = 1;
    sources[1].quality = 0;
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[2].yxValue = 2;
    sources[2].quality = 0;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYX(1, sources, output));
    
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::DISAGREE);
}

void TestVotingAndIEC61850::testVotingYX_Insufficient() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 只有一个有效源
    SourceData sources[3];
    sources[0].yxValue = 1;
    sources[0].quality = 0;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[1].status = static_cast<uint8_t>(SourceStatus::INVALID);
    sources[2].status = static_cast<uint8_t>(SourceStatus::TIMEOUT);
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYX(1, sources, output));
    
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::INSUFFICIENT);
    QCOMPARE(output.validSourceCount, static_cast<uint8_t>(1));
}

void TestVotingAndIEC61850::testVotingYC_Unanimous() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 创建三个一致的YC源
    SourceData sources[3];
    for (int i = 0; i < 3; i++) {
        sources[i].ycValue = 220.5f;
        sources[i].quality = 0;
        sources[i].status = static_cast<uint8_t>(SourceStatus::VALID);
        sources[i].timestamp = getCurrentTimestamp();
    }
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYC(2, sources, output)); // 使用YC表决组
    
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::UNANIMOUS);
    QCOMPARE(output.ycValue, 220.5f);
    QCOMPARE(output.validSourceCount, static_cast<uint8_t>(3));
}

void TestVotingAndIEC61850::testVotingYC_Deviation() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 创建三个有偏差的YC源
    SourceData sources[3];
    sources[0].ycValue = 220.0f;
    sources[0].quality = 0;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[1].ycValue = 221.0f;
    sources[1].quality = 0;
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    sources[2].ycValue = 230.0f; // 偏差较大
    sources[2].quality = 0;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    
    VotingOutput output;
    QVERIFY(m_votingEngine->voteYC(2, sources, output));
    
    // 结果应该是多数值（剔除偏差大的）
    QCOMPARE(static_cast<VotingResult>(output.result), VotingResult::MAJORITY);
    // 平均值应该在220-221附近
    QVERIFY(output.ycValue >= 219.0f && output.ycValue <= 222.0f);
}

void TestVotingAndIEC61850::testQuickVoteYX() {
    // 测试快速表决函数
    SourceData sources[3];
    sources[0].yxValue = 1;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[0].quality = 0;
    
    sources[1].yxValue = 1;
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[1].quality = 0;
    
    sources[2].yxValue = 0;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[2].quality = 0;
    
    uint8_t result;
    VotingResult vr = VotingEngine::quickVoteYX(sources, result);
    QCOMPARE(vr, VotingResult::MAJORITY);
    QCOMPARE(result, static_cast<uint8_t>(1));
}

void TestVotingAndIEC61850::testQuickVoteYC() {
    // 测试快速YC表决
    SourceData sources[3];
    sources[0].ycValue = 100.0f;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[0].quality = 0;
    
    sources[1].ycValue = 100.1f;
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[1].quality = 0;
    
    sources[2].ycValue = 100.2f;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[2].quality = 0;
    
    float result;
    VotingResult vr = VotingEngine::quickVoteYC(sources, result, 1.0f);
    QCOMPARE(vr, VotingResult::UNANIMOUS);
    QVERIFY(qAbs(result - 100.1f) < 0.2f);
}

void TestVotingAndIEC61850::testDeviationAlarm() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 设置告警回调
    m_alarmTriggered = false;
    m_votingEngine->setAlarmCallback([this](uint32_t groupId, DeviationLevel level, 
                                            const char* message) {
        m_alarmTriggered = true;
        m_alarmGroupId = groupId;
        m_alarmLevel = level;
    });
    
    // 添加一个新的表决组，偏差计数限制为2
    VotingConfig config;
    config.groupId = 100;
    strncpy(config.name, "AlarmTest", sizeof(config.name) - 1);
    config.sourceType = 0; // YX
    config.votingStrategy = 0;
    config.enableDeviation = 1;
    config.deviationCountLimit = 2; // 连续2次偏差触发告警
    
    m_votingEngine->addVotingGroup(config);
    
    // 第一次有偏差的表决
    SourceData sources[3];
    sources[0].yxValue = 0;
    sources[0].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[0].quality = 0;
    
    sources[1].yxValue = 1; // 与source[0]不一致
    sources[1].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[1].quality = 0;
    
    sources[2].yxValue = 0;
    sources[2].status = static_cast<uint8_t>(SourceStatus::VALID);
    sources[2].quality = 0;
    
    VotingOutput output;
    m_votingEngine->voteYX(100, sources, output);
    QCOMPARE(m_alarmTriggered, false); // 第一次不告警
    
    // 第二次有偏差的表决（连续）
    m_votingEngine->voteYX(100, sources, output);
    QCOMPARE(m_alarmTriggered, true);
    QCOMPARE(m_alarmGroupId, 100u);
}

void TestVotingAndIEC61850::testVotingStats() {
    QVERIFY(m_votingEngine != nullptr);
    
    // 获取统计
    VotingStats stats;
    QVERIFY(m_votingEngine->getVotingStats(1, stats));
    
    // 验证统计数量（根据之前的测试）
    QVERIFY(stats.totalVotes >= 4); // 至少4次表决
    
    // 重置统计
    QVERIFY(m_votingEngine->resetVotingStats(1));
    QVERIFY(m_votingEngine->getVotingStats(1, stats));
    QCOMPARE(stats.totalVotes, 0u);
}

// ==================== IEC 61850 映射测试 ====================

void TestVotingAndIEC61850::testIEC61850MapperCreation() {
    IEC61850Mapper::Config config;
    config.shmName = "/ipc_iec61850_test";
    config.maxMappings = 1000;
    config.maxLNs = 100;
    config.maxDataSets = 50;
    config.create = true;
    
    m_iecMapper = IEC61850Mapper::create(config);
    QVERIFY(m_iecMapper != nullptr);
    QCOMPARE(m_iecMapper->getMappingCount(), 0u);
    QCOMPARE(m_iecMapper->getLNCount(), 0u);
}

void TestVotingAndIEC61850::testAddDAMapping() {
    QVERIFY(m_iecMapper != nullptr);
    
    // 添加断路器位置映射
    DAMapping mapping;
    mapping.dataKey = makeKey(1, 100);
    strncpy(mapping.lnPrefix, "CB", sizeof(mapping.lnPrefix) - 1);
    mapping.lnClass = static_cast<uint8_t>(LNClass::XCBR);
    mapping.lnInst = 1;
    strncpy(mapping.doName, "Pos", sizeof(mapping.doName) - 1);
    strncpy(mapping.daName, "stVal", sizeof(mapping.daName) - 1);
    mapping.daType = static_cast<uint8_t>(DAType::DPS);
    mapping.fc = 1; // ST
    
    uint32_t idx = m_iecMapper->addDAMapping(mapping);
    QVERIFY(idx != INVALID_INDEX);
    QCOMPARE(m_iecMapper->getMappingCount(), 1u);
    
    // 添加质量映射
    mapping.dataKey = makeKey(1, 101);
    strncpy(mapping.daName, "q", sizeof(mapping.daName) - 1);
    mapping.daType = static_cast<uint8_t>(DAType::SPS);
    
    idx = m_iecMapper->addDAMapping(mapping);
    QVERIFY(idx != INVALID_INDEX);
    QCOMPARE(m_iecMapper->getMappingCount(), 2u);
}

void TestVotingAndIEC61850::testFindMapping() {
    QVERIFY(m_iecMapper != nullptr);
    
    // 通过key查找
    DAMapping mapping;
    QVERIFY(m_iecMapper->findMappingByKey(makeKey(1, 100), mapping));
    QCOMPARE(strcmp(mapping.doName, "Pos"), 0);
    QCOMPARE(strcmp(mapping.daName, "stVal"), 0);
    
    // 通过IEC引用查找
    QVERIFY(m_iecMapper->findMappingByRef("CBXCBR1", "Pos", "stVal", mapping));
    
    // 不存在的映射
    QVERIFY(!m_iecMapper->findMappingByKey(makeKey(999, 999), mapping));
}

void TestVotingAndIEC61850::testAddLogicalNode() {
    QVERIFY(m_iecMapper != nullptr);
    
    // 添加断路器逻辑节点
    LNMapping ln;
    strncpy(ln.lnRef, "CB1XCBR1", sizeof(ln.lnRef) - 1);
    ln.lnClass = static_cast<uint8_t>(LNClass::XCBR);
    ln.lnInst = 1;
    strncpy(ln.lnPrefix, "CB1", sizeof(ln.lnPrefix) - 1);
    ln.daCount = 2;
    ln.daStartIndex = 0;
    
    uint32_t idx = m_iecMapper->addLogicalNode(ln);
    QVERIFY(idx != INVALID_INDEX);
    QCOMPARE(m_iecMapper->getLNCount(), 1u);
    
    // 查找逻辑节点
    LNMapping retrieved;
    QVERIFY(m_iecMapper->findLogicalNodeByRef("CB1XCBR1", retrieved));
    QCOMPARE(retrieved.lnInst, static_cast<uint8_t>(1));
}

void TestVotingAndIEC61850::testCreateDataSet() {
    QVERIFY(m_iecMapper != nullptr);
    
    // 创建数据集
    DataSetDef dsDef;
    strncpy(dsDef.name, "dsTrip", sizeof(dsDef.name) - 1);
    strncpy(dsDef.ldInst, "LD0", sizeof(dsDef.ldInst) - 1);
    strncpy(dsDef.lnRef, "CB1XCBR1", sizeof(dsDef.lnRef) - 1);
    dsDef.memberCount = 2;
    dsDef.isDynamic = 0;
    
    // 数据集成员
    DataSetMember members[2];
    members[0].daMappingIndex = 0;
    strncpy(members[0].fcDa, "stVal", sizeof(members[0].fcDa) - 1);
    
    members[1].daMappingIndex = 1;
    strncpy(members[1].fcDa, "q", sizeof(members[1].fcDa) - 1);
    
    uint32_t idx = m_iecMapper->createDataSet(dsDef, members);
    QVERIFY(idx != INVALID_INDEX);
    
    // 验证数据集
    DataSetDef retrieved;
    std::vector<DataSetMember> retrievedMembers;
    QVERIFY(m_iecMapper->getDataSet(0, retrieved, retrievedMembers));
    QCOMPARE(retrieved.memberCount, static_cast<uint16_t>(2));
    QCOMPARE(retrievedMembers.size(), static_cast<size_t>(2));
    
    // 通过名称查找
    QVERIFY(m_iecMapper->findDataSetByName("dsTrip", retrieved));
}

void TestVotingAndIEC61850::testQualityConversion() {
    // 测试质量码转换
    
    // GOOD
    uint16_t iecQuality = IEC61850Mapper::toIEC61850Quality(0);
    QCOMPARE(iecQuality, static_cast<uint16_t>(QualityFlag::GOOD));
    QVERIFY(IEC61850Mapper::isQualityGood(iecQuality));
    
    // INVALID
    iecQuality = IEC61850Mapper::toIEC61850Quality(0xFF);
    QVERIFY(iecQuality & static_cast<uint16_t>(QualityFlag::INVALID));
    QVERIFY(!IEC61850Mapper::isQualityGood(iecQuality));
    
    // 反向转换
    uint8_t internalQuality = IEC61850Mapper::fromIEC61850Quality(
        static_cast<uint16_t>(QualityFlag::GOOD));
    QCOMPARE(internalQuality, static_cast<uint8_t>(0));
    
    internalQuality = IEC61850Mapper::fromIEC61850Quality(
        static_cast<uint16_t>(QualityFlag::INVALID));
    QCOMPARE(internalQuality, static_cast<uint8_t>(0xFF));
}

void TestVotingAndIEC61850::testExportMappings() {
    QVERIFY(m_iecMapper != nullptr);
    
    // 导出映射到CSV
    QVERIFY(m_iecMapper->exportMappingsToCSV("/tmp/test_mappings.csv"));
    
    // 导出数据集
    QVERIFY(m_iecMapper->exportDataSetsToCSV("/tmp/test_datasets.csv"));
    
    // 获取统计
    MappingStats stats = m_iecMapper->getStats();
    QCOMPARE(stats.totalMappings, 2u);
    QCOMPARE(stats.totalLNs, 1u);
    QCOMPARE(stats.totalDataSets, 1u);
}

QTEST_MAIN(TestVotingAndIEC61850)
#include "tst_voting_iec61850.moc"
