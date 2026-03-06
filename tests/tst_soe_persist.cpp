/**
 * @file tst_soe_persist.cpp
 * @brief SOE事件记录和持久化存储测试
 * 
 * 测试内容：
 * 1. SOE记录器创建/连接/销毁
 * 2. SOE事件记录（遥信变位、遥控执行、保护动作）
 * 3. SOE查询功能
 * 4. SOE统计功能
 * 5. SOE导出CSV
 * 6. 持久化初始化模式
 * 7. 快照保存/恢复
 * 8. 自动快照
 * 9. 备份管理
 */

#include <QtTest/QtTest>
#include "../include/SOERecorder.h"
#include "../include/PersistentStorage.h"
#include "../include/DataPoolClient.h"
#include <fstream>
#include <filesystem>

using namespace IPC;

namespace fs = std::filesystem;

class TestSOEAndPersistence : public QObject {
    Q_OBJECT

private:
    const char* SOE_NAME = "/ipc_test_soe";
    const char* POOL_NAME = "/ipc_test_pool_soe";
    const char* EVENT_NAME = "/ipc_test_event_soe";
    const char* SNAPSHOT_FILE = "/tmp/test_ipc_soe.snapshot";
    
    SOERecorder* m_soeRecorder;
    SharedDataPool* m_dataPool;
    DataPoolClient* m_client;

private slots:
    void initTestCase() {
        // 清理可能存在的旧共享内存
        shm_unlink(SOE_NAME);
        shm_unlink(POOL_NAME);
        shm_unlink(EVENT_NAME);
        
        // 删除旧快照文件
        if (fs::exists(SNAPSHOT_FILE)) {
            fs::remove(SNAPSHOT_FILE);
        }
        
        m_soeRecorder = nullptr;
        m_dataPool = nullptr;
        m_client = nullptr;
        
        qDebug() << "SOE和持久化测试开始";
    }

    void cleanupTestCase() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
        
        if (m_soeRecorder) {
            m_soeRecorder->destroy();
        }
        
        if (m_dataPool) {
            m_dataPool->destroy();
            delete m_dataPool;
        }
        
        // 清理共享内存
        shm_unlink(SOE_NAME);
        shm_unlink(POOL_NAME);
        shm_unlink(EVENT_NAME);
        
        // 清理快照文件
        if (fs::exists(SNAPSHOT_FILE)) {
            fs::remove(SNAPSHOT_FILE);
        }
        
        qDebug() << "SOE和持久化测试结束";
    }

    // ========== SOE 记录器测试 ==========

    void testSOERecorderCreate() {
        qDebug() << "测试: SOE记录器创建";
        
        m_soeRecorder = SOERecorder::create(SOE_NAME, 1000);
        QVERIFY(m_soeRecorder != nullptr);
        QVERIFY(m_soeRecorder->isValid());
        
        SOEStats stats = m_soeRecorder->getStats();
        QCOMPARE(stats.capacity, 1000u);
        QCOMPARE(stats.currentLoad, 0u);
        QCOMPARE(stats.totalRecords, 0ull);
    }

    void testSOERecordYXChange() {
        qDebug() << "测试: SOE遥信变位记录";
        
        // 记录遥信变位
        uint32_t pointKey = 0x01020304;  // 设备地址0x0102, 点号0x0304
        Result result = m_soeRecorder->recordYXChange(pointKey, 0, 1, 128);
        QCOMPARE(result, Result::OK);
        
        // 验证统计
        SOEStats stats = m_soeRecorder->getStats();
        QCOMPARE(stats.totalRecords, 1ull);
        QCOMPARE(stats.currentLoad, 1u);
    }

    void testSOERecordMultiple() {
        qDebug() << "测试: SOE多条记录";
        
        // 记录多条事件
        for (int i = 0; i < 100; i++) {
            uint32_t pointKey = i;
            m_soeRecorder->recordYXChange(pointKey, i % 2, (i + 1) % 2, 100);
            m_soeRecorder->recordYKExecute(pointKey + 1000, i % 4, 150);
        }
        
        SOEStats stats = m_soeRecorder->getStats();
        QCOMPARE(stats.totalRecords, 201ull);  // 1 + 100 + 100
    }

    void testSOEQueryByTimeRange() {
        qDebug() << "测试: SOE时间范围查询";
        
        uint64_t now = getAbsoluteTimeNs();
        uint64_t startTime = now - 10000000000ULL;  // 10秒前
        uint64_t endTime = now + 1000000000ULL;     // 1秒后
        
        SOERecord records[50];
        uint32_t count = 0;
        
        Result result = m_soeRecorder->getByTimeRange(startTime, endTime, 
                                                       records, count, 50);
        QCOMPARE(result, Result::OK);
        QVERIFY(count > 0);
        QVERIFY(count <= 50);
        
        // 验证时间范围
        for (uint32_t i = 0; i < count; i++) {
            QVERIFY(records[i].absoluteTime >= startTime);
            QVERIFY(records[i].absoluteTime <= endTime);
        }
    }

    void testSOEQueryWithCondition() {
        qDebug() << "测试: SOE条件查询";
        
        SOEQueryCondition condition;
        condition.pointType = static_cast<uint8_t>(PointType::YK);  // 只查询遥控
        condition.maxRecords = 30;
        condition.reverseOrder = true;
        
        SOERecord records[50];
        uint32_t count = 0;
        
        Result result = m_soeRecorder->query(condition, records, count, 50);
        QCOMPARE(result, Result::OK);
        QVERIFY(count > 0);
        
        // 验证都是遥控事件
        for (uint32_t i = 0; i < count; i++) {
            QCOMPARE(records[i].pointType, static_cast<uint8_t>(PointType::YK));
        }
    }

    void testSOEGetLatest() {
        qDebug() << "测试: SOE获取最新记录";
        
        SOERecord records[10];
        uint32_t count = 0;
        
        Result result = m_soeRecorder->getLatest(10, records, count);
        QCOMPARE(result, Result::OK);
        QVERIFY(count > 0);
        QVERIFY(count <= 10);
    }

    void testSOEExportCSV() {
        qDebug() << "测试: SOE导出CSV";
        
        const char* csvFile = "/tmp/test_soe_export.csv";
        
        Result result = m_soeRecorder->exportToCSV(csvFile);
        QCOMPARE(result, Result::OK);
        
        // 验证文件存在
        QVERIFY(fs::exists(csvFile));
        
        // 验证文件内容
        std::ifstream file(csvFile);
        std::string line;
        QVERIFY(std::getline(file, line));  // 读取表头
        QVERIFY(line.find("时间戳") != std::string::npos);
        
        file.close();
        fs::remove(csvFile);
    }

    void testSOERecorderConnect() {
        qDebug() << "测试: SOE记录器连接";
        
        // 连接到已存在的SOE记录器
        SOERecorder* recorder2 = SOERecorder::connect(SOE_NAME);
        QVERIFY(recorder2 != nullptr);
        QVERIFY(recorder2->isValid());
        
        // 验证数据一致
        SOEStats stats1 = m_soeRecorder->getStats();
        SOEStats stats2 = recorder2->getStats();
        QCOMPARE(stats1.totalRecords, stats2.totalRecords);
        QCOMPARE(stats1.capacity, stats2.capacity);
        
        recorder2->disconnect();
    }

    // ========== 持久化存储测试 ==========

    void testPersistentStorageCreate() {
        qDebug() << "测试: 持久化存储创建";
        
        // 先创建数据池
        m_dataPool = SharedDataPool::create(POOL_NAME, 100, 100, 50, 50);
        QVERIFY(m_dataPool != nullptr);
        QVERIFY(m_dataPool->isValid());
        
        // 创建持久化配置
        PersistentConfig config;
        std::strcpy(config.snapshotPath, SNAPSHOT_FILE);
        config.snapshotIntervalMs = 1000;  // 1秒
        config.enableAutoSnapshot = false;  // 测试时不启用自动快照
        
        // 创建持久化存储
        m_persistentStorage = new PersistentStorage(m_dataPool, config);
        QVERIFY(m_persistentStorage != nullptr);
        
        // 验证配置
        QCOMPARE(m_persistentStorage->getConfig().snapshotIntervalMs, 1000u);
        QCOMPARE(m_persistentStorage->isAutoSnapshotEnabled(), false);
    }

    void testSnapshotSaveAndLoad() {
        qDebug() << "测试: 快照保存和加载";
        
        // 先注册数据点
        uint32_t yxIdx1, yxIdx2, ycIdx1;
        m_dataPool->registerKey(0x00010001, PointType::YX, yxIdx1);
        m_dataPool->registerKey(0x00010002, PointType::YX, yxIdx2);
        m_dataPool->registerKey(0x00020001, PointType::YC, ycIdx1);
        
        // 设置一些数据
        m_dataPool->setYX(0x00010001, 1, getCurrentTimestamp(), 0);
        m_dataPool->setYX(0x00010002, 0, getCurrentTimestamp(), 0);
        m_dataPool->setYC(0x00020001, 123.45f, getCurrentTimestamp(), 0);
        
        // 保存快照
        Result result = m_persistentStorage->saveSnapshot(SNAPSHOT_FILE);
        QCOMPARE(result, Result::OK);
        
        // 验证文件存在
        QVERIFY(fs::exists(SNAPSHOT_FILE));
        
        // 修改数据
        m_dataPool->setYX(0x00010001, 0, getCurrentTimestamp(), 0);
        m_dataPool->setYC(0x00020001, 999.99f, getCurrentTimestamp(), 0);
        
        // 恢复快照
        result = m_persistentStorage->restore(SNAPSHOT_FILE);
        QCOMPARE(result, Result::OK);
        
        // 验证数据已恢复
        uint8_t yxValue;
        uint64_t ts;
        uint8_t quality;
        m_dataPool->getYX(0x00010001, yxValue, ts, quality);
        QCOMPARE(yxValue, 1u);  // 恢复为原来的值
        
        float ycValue;
        m_dataPool->getYC(0x00020001, ycValue, ts, quality);
        QVERIFY(qAbs(ycValue - 123.45f) < 0.01f);
    }

    void testInitModes() {
        qDebug() << "测试: 初始化模式";
        
        PersistentConfig config;
        
        // 测试各种初始化模式（使用LOAD_DEFAULT以避免需要快照文件）
        config.yxInitMode = InitMode::LOAD_DEFAULT;
        config.ycInitMode = InitMode::LOAD_DEFAULT;
        config.dzInitMode = InitMode::INVALIDATE;
        config.ykInitMode = InitMode::WAIT_FOR_FRESH;
        config.enableAutoSnapshot = false;
        
        // 创建新的持久化存储
        PersistentStorage* ps = new PersistentStorage(m_dataPool, config);
        
        // 初始化
        Result result = ps->initialize();
        QCOMPARE(result, Result::OK);
        
        delete ps;
    }

    void testAutoSnapshot() {
        qDebug() << "测试: 自动快照";
        
        PersistentConfig config;
        config.snapshotIntervalMs = 100;  // 100ms间隔
        config.enableAutoSnapshot = true;
        std::strcpy(config.snapshotPath, "/tmp/test_auto_snapshot.dat");
        
        PersistentStorage* ps = new PersistentStorage(m_dataPool, config);
        QVERIFY(ps->isAutoSnapshotEnabled());
        
        // 等待自动快照
        QThread::msleep(300);
        
        // 验证已执行快照
        QVERIFY(ps->getLastSnapshotTime() > 0);
        
        // 禁用自动快照
        ps->enableAutoSnapshot(false);
        QVERIFY(!ps->isAutoSnapshotEnabled());
        
        delete ps;
        
        // 清理
        if (fs::exists("/tmp/test_auto_snapshot.dat")) {
            fs::remove("/tmp/test_auto_snapshot.dat");
        }
    }

    void testBackupManagement() {
        qDebug() << "测试: 备份管理";
        
        // 创建备份目录
        std::string backupDir = "/tmp/test_ipc_backup";
        fs::create_directories(backupDir);
        
        PersistentConfig config;
        std::strcpy(config.backupPath, backupDir.c_str());
        std::strcpy(config.snapshotPath, SNAPSHOT_FILE);
        config.maxSnapshotFiles = 3;
        
        PersistentStorage* ps = new PersistentStorage(m_dataPool, config);
        
        // 创建多个备份
        ps->createBackup();
        QThread::msleep(10);  // 确保时间戳不同
        ps->createBackup();
        QThread::msleep(10);
        ps->createBackup();
        
        // 获取备份列表
        auto backupList = ps->getBackupList();
        QVERIFY(backupList.size() >= 3);
        
        // 从备份恢复
        if (!backupList.empty()) {
            Result result = ps->restoreFromBackup(0);  // 恢复最新备份
            QCOMPARE(result, Result::OK);
        }
        
        // 清理旧备份
        ps->cleanupOldBackups(2);
        backupList = ps->getBackupList();
        QVERIFY(backupList.size() <= 2);
        
        delete ps;
        
        // 清理备份目录
        fs::remove_all(backupDir);
    }

    void testHasValidSnapshot() {
        qDebug() << "测试: 检查有效快照";
        
        // 快照文件存在
        QVERIFY(m_persistentStorage->hasValidSnapshot());
        
        // 检查快照信息
        PersistHeader header;
        bool result = m_persistentStorage->getSnapshotInfo(SNAPSHOT_FILE, header);
        QVERIFY(result);
        QCOMPARE(header.magic, PERSIST_MAGIC);
    }

    // ========== 客户端集成测试 ==========

    void testDataPoolClientWithSOE() {
        qDebug() << "测试: DataPoolClient集成SOE";
        
        DataPoolClient::Config config;
        config.poolName = "/ipc_test_client_pool";
        config.eventName = "/ipc_test_client_event";
        config.soeName = "/ipc_test_client_soe";
        config.processName = "test_client";
        config.yxCount = 100;
        config.ycCount = 100;
        config.dzCount = 50;
        config.ykCount = 50;
        config.soeCapacity = 1000;
        config.create = true;
        config.enableSOE = true;
        config.enablePersistence = true;
        
        // 设置初始化模式为加载默认值（因为没有快照文件）
        config.persistConfig.yxInitMode = InitMode::LOAD_DEFAULT;
        config.persistConfig.ycInitMode = InitMode::LOAD_DEFAULT;
        config.persistConfig.dzInitMode = InitMode::LOAD_DEFAULT;
        config.persistConfig.ykInitMode = InitMode::LOAD_DEFAULT;
        config.persistConfig.enableAutoSnapshot = false;
        
        m_client = DataPoolClient::init(config);
        QVERIFY(m_client != nullptr);
        QVERIFY(m_client->isValid());
        
        // 测试SOE记录
        QVERIFY(m_client->recordSOEYXChange(0x01020304, 0, 1, 128));
        
        // 测试SOE统计
        SOEStats stats = m_client->getSOEStats();
        QVERIFY(stats.totalRecords > 0);
        
        // 测试持久化初始化（使用默认值模式）
        QVERIFY(m_client->initializeData());
        
        // 测试手动快照
        QVERIFY(m_client->triggerSnapshot());
        
        // 测试检查快照
        QVERIFY(m_client->hasValidSnapshot());
        
        // 清理
        shm_unlink("/ipc_test_client_pool");
        shm_unlink("/ipc_test_client_event");
        shm_unlink("/ipc_test_client_soe");
    }

private:
    PersistentStorage* m_persistentStorage;
};

QTEST_MAIN(TestSOEAndPersistence)
#include "tst_soe_persist.moc"
