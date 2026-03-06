/**
 * @file comm_process/main.cpp
 * @brief 通信进程示例 - 模拟数据采集与推送
 * 
 * 功能：
 * - 创建共享数据池
 * - 模拟从设备采集数据
 * - 更新数据并发布事件
 */

#include <QCoreApplication>
#include <QTimer>
#include <QCommandLineParser>
#include <iostream>
#include <random>
#include "../../include/DataPoolClient.h"

using namespace IPC;

class CommProcess : public QObject {
    Q_OBJECT

public:
    CommProcess(QObject* parent = nullptr) : QObject(parent), m_client(nullptr), m_timer(nullptr) {}
    
    ~CommProcess() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
    }
    
    bool init(uint32_t yxCount, uint32_t ycCount) {
        DataPoolClient::Config config;
        config.poolName = "/ipc_data_pool";
        config.eventName = "/ipc_events";
        config.processName = "comm_process";
        config.yxCount = yxCount;
        config.ycCount = ycCount;
        config.dzCount = 100;
        config.ykCount = 100;
        config.eventCapacity = 10000;
        config.create = true;
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            std::cerr << "Failed to initialize data pool client" << std::endl;
            return false;
        }
        
        // 注册点位
        for (uint32_t i = 0; i < yxCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(1, i), PointType::YX, idx);
        }
        
        for (uint32_t i = 0; i < ycCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(2, i), PointType::YC, idx);
        }
        
        std::cout << "Comm process initialized:" << std::endl;
        std::cout << "  YX points: " << yxCount << std::endl;
        std::cout << "  YC points: " << ycCount << std::endl;
        
        return true;
    }
    
    void start(int intervalMs) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &CommProcess::onTimer);
        m_timer->start(intervalMs);
        
        std::cout << "Data update started, interval: " << intervalMs << "ms" << std::endl;
    }
    
private slots:
    void onTimer() {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<int> yxDist(0, 1);
        static std::uniform_real_distribution<float> ycDist(0.0f, 100.0f);
        
        // 随机更新一些 YX
        for (int i = 0; i < 10; i++) {
            uint32_t idx = rng() % m_client->getDataPool()->getYXCount();
            uint8_t value = yxDist(rng);
            m_client->setYXByIndex(idx, value);
        }
        
        // 随机更新一些 YC
        for (int i = 0; i < 20; i++) {
            uint32_t idx = rng() % m_client->getDataPool()->getYCCount();
            float value = ycDist(rng);
            m_client->setYCByIndex(idx, value);
        }
        
        // 更新心跳
        m_client->updateHeartbeat();
    }

private:
    DataPoolClient* m_client;
    QTimer* m_timer;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("comm_process");
    QCoreApplication::setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Communication Process - Data Acquisition Simulator");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption yxOption("yx", "YX point count", "count", "1000");
    parser.addOption(yxOption);
    
    QCommandLineOption ycOption("yc", "YC point count", "count", "1000");
    parser.addOption(ycOption);
    
    QCommandLineOption intervalOption("interval", "Update interval (ms)", "ms", "100");
    parser.addOption(intervalOption);
    
    parser.process(app);
    
    uint32_t yxCount = parser.value(yxOption).toUInt();
    uint32_t ycCount = parser.value(ycOption).toUInt();
    int interval = parser.value(intervalOption).toInt();
    
    CommProcess comm;
    if (!comm.init(yxCount, ycCount)) {
        return 1;
    }
    
    comm.start(interval);
    
    return app.exec();
}

#include "main.moc"
