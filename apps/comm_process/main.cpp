/**
 * @file comm_process/main.cpp
 * @brief 通信进程 - 可视化数据采集模拟器
 * 
 * 功能：
 * - 创建共享数据池
 * - 模拟数据采集
 * - 实时数据更新
 * - 设备通信状态监控
 */

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QProgressBar>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QSplitter>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <iostream>
#include <random>
#include <atomic>
#include "../../include/DataPoolClient.h"
#include "../../include/ProcessMonitor.h"

using namespace IPC;

/**
 * @brief 通信进程主窗口
 */
class CommProcessWindow : public QMainWindow {
    Q_OBJECT

public:
    CommProcessWindow(QWidget* parent = nullptr) 
        : QMainWindow(parent), m_client(nullptr), m_processMonitor(nullptr), m_running(false), 
          m_yxCount(0), m_ycCount(0) {
        m_processMonitor = new ProcessMonitor();
        setupUI();
        setupMenu();
    }
    
    ~CommProcessWindow() {
        stopSimulation();
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
        if (m_processMonitor) {
            delete m_processMonitor;
        }
    }

private:
    void setupUI() {
        setWindowTitle("通信进程 - 数据采集模拟器");
        resize(1200, 800);
        
        // 中央部件
        QWidget* central = new QWidget(this);
        setCentralWidget(central);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        
        // 顶部配置区
        QGroupBox* configGroup = new QGroupBox("数据池配置", this);
        QGridLayout* configLayout = new QGridLayout(configGroup);
        
        int row = 0;
        configLayout->addWidget(new QLabel("YX点数:"), row, 0);
        m_yxCountSpin = new QSpinBox(this);
        m_yxCountSpin->setRange(10, 1000000);
        m_yxCountSpin->setValue(100);
        m_yxCountSpin->setSingleStep(1000);
        configLayout->addWidget(m_yxCountSpin, row, 1);
        
        configLayout->addWidget(new QLabel("YC点数:"), row, 2);
        m_ycCountSpin = new QSpinBox(this);
        m_ycCountSpin->setRange(10, 1000000);
        m_ycCountSpin->setValue(100);
        m_ycCountSpin->setSingleStep(1000);
        configLayout->addWidget(m_ycCountSpin, row, 3);
        
        configLayout->addWidget(new QLabel("DZ点数:"), row, 4);
        m_dzCountSpin = new QSpinBox(this);
        m_dzCountSpin->setRange(0, 500000);
        m_dzCountSpin->setValue(50);
        m_dzCountSpin->setSingleStep(1000);
        configLayout->addWidget(m_dzCountSpin, row, 5);
        
        configLayout->addWidget(new QLabel("YK点数:"), row, 6);
        m_ykCountSpin = new QSpinBox(this);
        m_ykCountSpin->setRange(0, 500000);
        m_ykCountSpin->setValue(50);
        m_ykCountSpin->setSingleStep(1000);
        configLayout->addWidget(m_ykCountSpin, row, 7);
        
        row++;
        configLayout->addWidget(new QLabel("更新间隔(ms):"), row, 0);
        m_intervalSpin = new QSpinBox(this);
        m_intervalSpin->setRange(10, 10000);
        m_intervalSpin->setValue(100);
        configLayout->addWidget(m_intervalSpin, row, 1);
        
        m_createBtn = new QPushButton("创建数据池", this);
        m_createBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
        configLayout->addWidget(m_createBtn, row, 2);
        
        m_destroyBtn = new QPushButton("销毁数据池", this);
        m_destroyBtn->setEnabled(false);
        m_destroyBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
        configLayout->addWidget(m_destroyBtn, row, 3);
        
        configLayout->setColumnStretch(8, 1);
        mainLayout->addWidget(configGroup);
        
        // 中间控制区
        QGroupBox* controlGroup = new QGroupBox("数据采集控制", this);
        QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
        
        m_startBtn = new QPushButton("▶ 开始采集", this);
        m_startBtn->setEnabled(false);
        m_startBtn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold; padding: 8px;");
        controlLayout->addWidget(m_startBtn);
        
        m_stopBtn = new QPushButton("⏹ 停止采集", this);
        m_stopBtn->setEnabled(false);
        m_stopBtn->setStyleSheet("background-color: #FF9800; color: white; font-weight: bold; padding: 8px;");
        controlLayout->addWidget(m_stopBtn);
        
        controlLayout->addWidget(new QLabel("YX变化率:"));
        m_yxChangeRate = new QSpinBox(this);
        m_yxChangeRate->setRange(1, 1000);
        m_yxChangeRate->setValue(5);
        controlLayout->addWidget(m_yxChangeRate);
        
        controlLayout->addWidget(new QLabel("YC更新数:"));
        m_ycUpdateCount = new QSpinBox(this);
        m_ycUpdateCount->setRange(1, 1000);
        m_ycUpdateCount->setValue(10);
        controlLayout->addWidget(m_ycUpdateCount);
        
        controlLayout->addStretch();
        mainLayout->addWidget(controlGroup);
        
        // Tab页面
        QTabWidget* tabWidget = new QTabWidget(this);
        
        // 数据监控页
        QWidget* dataTab = new QWidget();
        QVBoxLayout* dataLayout = new QVBoxLayout(dataTab);
        
        // 统计面板
        QGridLayout* statsGrid = new QGridLayout();
        statsGrid->addWidget(new QLabel("YX总数:"), 0, 0);
        m_yxTotalLabel = new QLabel("0");
        m_yxTotalLabel->setStyleSheet("font-weight: bold; color: #F44336;");
        statsGrid->addWidget(m_yxTotalLabel, 0, 1);
        
        statsGrid->addWidget(new QLabel("YC总数:"), 0, 2);
        m_ycTotalLabel = new QLabel("0");
        m_ycTotalLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        statsGrid->addWidget(m_ycTotalLabel, 0, 3);
        
        statsGrid->addWidget(new QLabel("YX变化数:"), 0, 4);
        m_yxChangesLabel = new QLabel("0");
        m_yxChangesLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
        statsGrid->addWidget(m_yxChangesLabel, 0, 5);
        
        statsGrid->addWidget(new QLabel("YC更新数:"), 0, 6);
        m_ycUpdatesLabel = new QLabel("0");
        m_ycUpdatesLabel->setStyleSheet("font-weight: bold; color: #9C27B0;");
        statsGrid->addWidget(m_ycUpdatesLabel, 0, 7);
        
        // 系统监控信息（第二行）
        statsGrid->addWidget(new QLabel("进程CPU:"), 1, 0);
        m_procCpuLabel = new QLabel("0.0%");
        m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
        statsGrid->addWidget(m_procCpuLabel, 1, 1);
        
        statsGrid->addWidget(new QLabel("进程内存:"), 1, 2);
        m_procMemLabel = new QLabel("0 MB");
        m_procMemLabel->setStyleSheet("font-weight: bold; color: #673AB7;");
        statsGrid->addWidget(m_procMemLabel, 1, 3);
        
        statsGrid->addWidget(new QLabel("系统CPU:"), 1, 4);
        m_sysCpuLabel = new QLabel("0.0%");
        m_sysCpuLabel->setStyleSheet("font-weight: bold; color: #00BCD4;");
        statsGrid->addWidget(m_sysCpuLabel, 1, 5);
        
        statsGrid->addWidget(new QLabel("系统内存:"), 1, 6);
        m_sysMemLabel = new QLabel("0.0%");
        m_sysMemLabel->setStyleSheet("font-weight: bold; color: #8BC34A;");
        statsGrid->addWidget(m_sysMemLabel, 1, 7);
        
        statsGrid->setColumnStretch(8, 1);
        dataLayout->addLayout(statsGrid);
        
        // 数据表格
        QSplitter* dataSplitter = new QSplitter(Qt::Horizontal, this);
        
        // YX数据表
        QGroupBox* yxGroup = new QGroupBox("YX数据 (遥信)", this);
        QVBoxLayout* yxLayout = new QVBoxLayout(yxGroup);
        m_yxTable = new QTableWidget(this);
        m_yxTable->setColumnCount(5);
        m_yxTable->setHorizontalHeaderLabels({"索引", "Key", "值", "时间戳", "质量"});
        m_yxTable->horizontalHeader()->setStretchLastSection(true);
        yxLayout->addWidget(m_yxTable);
        dataSplitter->addWidget(yxGroup);
        
        // YC数据表
        QGroupBox* ycGroup = new QGroupBox("YC数据 (遥测)", this);
        QVBoxLayout* ycLayout = new QVBoxLayout(ycGroup);
        m_ycTable = new QTableWidget(this);
        m_ycTable->setColumnCount(5);
        m_ycTable->setHorizontalHeaderLabels({"索引", "Key", "值", "时间戳", "质量"});
        m_ycTable->horizontalHeader()->setStretchLastSection(true);
        ycLayout->addWidget(m_ycTable);
        dataSplitter->addWidget(ycGroup);
        
        dataLayout->addWidget(dataSplitter);
        tabWidget->addTab(dataTab, "数据监控");
        
        // 进程状态页
        QWidget* procTab = new QWidget();
        QVBoxLayout* procLayout = new QVBoxLayout(procTab);
        m_procTable = new QTableWidget(this);
        m_procTable->setColumnCount(5);
        m_procTable->setHorizontalHeaderLabels({"进程ID", "PID", "名称", "最后心跳", "状态"});
        m_procTable->horizontalHeader()->setStretchLastSection(true);
        procLayout->addWidget(m_procTable);
        tabWidget->addTab(procTab, "进程状态");
        
        // YK遥控监控页
        QWidget* ykTab = new QWidget();
        QVBoxLayout* ykLayout = new QVBoxLayout(ykTab);
        
        // YK统计面板
        QGridLayout* ykStatsGrid = new QGridLayout();
        ykStatsGrid->addWidget(new QLabel("YK总数:"), 0, 0);
        m_ykTotalLabel = new QLabel("0");
        m_ykTotalLabel->setStyleSheet("font-weight: bold; color: #9C27B0;");
        ykStatsGrid->addWidget(m_ykTotalLabel, 0, 1);
        
        ykStatsGrid->addWidget(new QLabel("收到命令:"), 0, 2);
        m_ykCmdCountLabel = new QLabel("0");
        m_ykCmdCountLabel->setStyleSheet("font-weight: bold; color: #F44336;");
        ykStatsGrid->addWidget(m_ykCmdCountLabel, 0, 3);
        
        ykStatsGrid->addWidget(new QLabel("执行成功:"), 0, 4);
        m_ykSuccessLabel = new QLabel("0");
        m_ykSuccessLabel->setStyleSheet("font-weight: bold; color: #4CAF50;");
        ykStatsGrid->addWidget(m_ykSuccessLabel, 0, 5);
        
        ykStatsGrid->setColumnStretch(6, 1);
        ykLayout->addLayout(ykStatsGrid);
        
        // YK命令表
        m_ykTable = new QTableWidget(this);
        m_ykTable->setColumnCount(6);
        m_ykTable->setHorizontalHeaderLabels({"时间", "Key", "命令值", "执行结果", "反馈时间", "状态"});
        m_ykTable->horizontalHeader()->setStretchLastSection(true);
        ykLayout->addWidget(m_ykTable);
        
        tabWidget->addTab(ykTab, "YK遥控监控");
        
        // 事件发布统计页
        QWidget* eventTab = new QWidget();
        QVBoxLayout* eventLayout = new QVBoxLayout(eventTab);
        
        QGridLayout* eventStatsGrid = new QGridLayout();
        eventStatsGrid->addWidget(new QLabel("YX事件发布:"), 0, 0);
        m_yxEventLabel = new QLabel("0");
        m_yxEventLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
        eventStatsGrid->addWidget(m_yxEventLabel, 0, 1);
        
        eventStatsGrid->addWidget(new QLabel("YC事件发布:"), 0, 2);
        m_ycEventLabel = new QLabel("0");
        m_ycEventLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        eventStatsGrid->addWidget(m_ycEventLabel, 0, 3);
        
        eventStatsGrid->addWidget(new QLabel("SOE记录:"), 0, 4);
        m_soeCountLabel = new QLabel("0");
        m_soeCountLabel->setStyleSheet("font-weight: bold; color: #9C27B0;");
        eventStatsGrid->addWidget(m_soeCountLabel, 0, 5);
        
        eventStatsGrid->setColumnStretch(6, 1);
        eventLayout->addLayout(eventStatsGrid);
        
        m_eventTable = new QTableWidget(this);
        m_eventTable->setColumnCount(5);
        m_eventTable->setHorizontalHeaderLabels({"时间", "类型", "Key", "旧值", "新值"});
        m_eventTable->horizontalHeader()->setStretchLastSection(true);
        eventLayout->addWidget(m_eventTable);
        
        tabWidget->addTab(eventTab, "事件发布");
        
        mainLayout->addWidget(tabWidget);
        
        // 底部日志区
        QGroupBox* logGroup = new QGroupBox("运行日志", this);
        QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
        m_logEdit = new QTextEdit(this);
        m_logEdit->setReadOnly(true);
        m_logEdit->setMaximumHeight(150);
        m_logEdit->setStyleSheet("font-family: Consolas; font-size: 10px;");
        logLayout->addWidget(m_logEdit);
        
        QHBoxLayout* logBtnLayout = new QHBoxLayout();
        QPushButton* clearLogBtn = new QPushButton("清除日志", this);
        connect(clearLogBtn, &QPushButton::clicked, this, [this]() {
            m_logEdit->clear();
            appendLog("日志已清除");
        });
        logBtnLayout->addWidget(clearLogBtn);
        logBtnLayout->addStretch();
        logLayout->addLayout(logBtnLayout);
        
        mainLayout->addWidget(logGroup);
        
        // 状态栏
        m_statusLabel = new QLabel("未连接", this);
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        statusBar()->addWidget(m_statusLabel);
        
        // 连接信号
        connect(m_createBtn, &QPushButton::clicked, this, &CommProcessWindow::createDataPool);
        connect(m_destroyBtn, &QPushButton::clicked, this, &CommProcessWindow::destroyDataPool);
        connect(m_startBtn, &QPushButton::clicked, this, &CommProcessWindow::startSimulation);
        connect(m_stopBtn, &QPushButton::clicked, this, &CommProcessWindow::stopSimulation);
        
        // 刷新定时器
        m_refreshTimer = new QTimer(this);
        connect(m_refreshTimer, &QTimer::timeout, this, &CommProcessWindow::refreshData);
    }
    
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
        
        QAction* exitAction = new QAction("退出(&X)", this);
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
        fileMenu->addAction(exitAction);
        
        QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
        QAction* aboutAction = new QAction("关于(&A)", this);
        connect(aboutAction, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, "关于", 
                "通信进程 - 数据采集模拟器\n\n"
                "用于创建共享数据池并模拟数据采集\n"
                "版本: 1.0.0");
        });
        helpMenu->addAction(aboutAction);
    }

private slots:
    void createDataPool() {
        if (m_client) {
            appendLog("数据池已存在，请先销毁");
            return;
        }
        
        m_yxCount = m_yxCountSpin->value();
        m_ycCount = m_ycCountSpin->value();
        uint32_t dzCount = m_dzCountSpin->value();
        uint32_t ykCount = m_ykCountSpin->value();
        
        // 预估内存需求
        // YX: 10 bytes/点 (1值 + 8时间戳 + 1质量)
        // YC: 13 bytes/点 (4值 + 8时间戳 + 1质量)
        // DZ: 13 bytes/点
        // YK: 10 bytes/点
        // 哈希表 + 索引表: 约 (总点数 * 2 * 4) + (总点数 * 24) bytes
        uint64_t totalPoints = (uint64_t)m_yxCount + m_ycCount + dzCount + ykCount;
        uint64_t dataPoolSize = m_yxCount * 10 + m_ycCount * 13 + dzCount * 13 + ykCount * 10;
        uint64_t indexSize = totalPoints * 2 * 4 + totalPoints * 24;
        uint64_t estimatedSize = dataPoolSize + indexSize + 1024 * 1024; // 加上头部和额外开销
        
        QString sizeInfo = QString("预估内存需求: %1 MB (数据池: %2 MB, 索引: %3 MB)")
            .arg(estimatedSize / 1024 / 1024)
            .arg(dataPoolSize / 1024 / 1024)
            .arg(indexSize / 1024 / 1024);
        appendLog(sizeInfo);
        
        // 检查是否超过1GB，给出警告
        if (estimatedSize > 1024ULL * 1024 * 1024) {
            QMessageBox::StandardButton reply = QMessageBox::warning(this, 
                "内存警告", 
                QString("数据池将使用约 %1 GB内存，是否继续?\n\n点数统计:\n- YX: %2\n- YC: %3\n- DZ: %4\n- YK: %5\n- 总计: %6")
                    .arg(estimatedSize / 1024.0 / 1024 / 1024, 0, 'f', 2)
                    .arg(m_yxCount).arg(m_ycCount).arg(dzCount).arg(ykCount).arg(totalPoints),
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                appendLog("创建已取消");
                return;
            }
        }
        
        DataPoolClient::Config config;
        config.poolName = "/ipc_data_pool";
        config.eventName = "/ipc_events";
        config.soeName = "/ipc_soe";
        config.processName = "comm_process";
        config.yxCount = m_yxCount;
        config.ycCount = m_ycCount;
        config.dzCount = dzCount;
        config.ykCount = ykCount;
        config.eventCapacity = 10000;
        config.soeCapacity = 100000;
        config.create = true;
        config.enablePersistence = true;
        config.enableSOE = true;
        config.enableVoting = true;
        config.enableIEC61850 = true;
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            appendLog("错误: 创建数据池失败! 请检查系统共享内存限制。");
            appendLog("提示: 运行 'cat /proc/sys/kernel/shmmax' 查看最大共享内存限制");
            QMessageBox::critical(this, "错误", 
                "创建数据池失败!\n\n可能原因:\n"
                "1. 系统共享内存限制不足\n"
                "2. /dev/shm 空间不足\n"
                "3. 内存不足\n\n"
                "请尝试:\n"
                "- 减少点数配置\n"
                "- 检查 'df -h /dev/shm'\n"
                "- 运行 './run_apps.sh clean' 清理旧共享内存");
            return;
        }
        
        // 注册点位
        appendLog(QString("注册 YX 点位: %1").arg(m_yxCount));
        for (uint32_t i = 0; i < m_yxCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(1, i), PointType::YX, idx);
        }
        
        appendLog(QString("注册 YC 点位: %1").arg(m_ycCount));
        for (uint32_t i = 0; i < m_ycCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(2, i), PointType::YC, idx);
        }
        
        appendLog(QString("注册 DZ 点位: %1").arg(dzCount));
        for (uint32_t i = 0; i < dzCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(3, i), PointType::DZ, idx);
        }
        
        appendLog(QString("注册 YK 点位: %1").arg(ykCount));
        for (uint32_t i = 0; i < ykCount; i++) {
            uint32_t idx;
            m_client->registerPoint(makeKey(4, i), PointType::YK, idx);
        }
        
        // 订阅YK事件，接收遥控命令
        m_ykSubId = m_client->subscribe([this](const Event& event) {
            if (event.pointType == PointType::YK) {
                QMetaObject::invokeMethod(this, [this, event]() {
                    handleYKEvent(event);
                });
            }
        });
        
        if (m_ykSubId != INVALID_INDEX) {
            appendLog(QString("YK事件订阅成功, ID: %1").arg(m_ykSubId));
        }
        
        // 启动心跳
        m_client->startHeartbeat(1000);
        
        m_createBtn->setEnabled(false);
        m_destroyBtn->setEnabled(true);
        m_startBtn->setEnabled(true);
        m_yxCountSpin->setEnabled(false);
        m_ycCountSpin->setEnabled(false);
        m_dzCountSpin->setEnabled(false);
        m_ykCountSpin->setEnabled(false);
        
        m_statusLabel->setText("已连接 - 数据池已创建");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        // 开始刷新
        m_refreshTimer->start(500);
        
        appendLog("数据池创建成功!");
        QMessageBox::information(this, "成功", "数据池创建成功!");
    }
    
    void destroyDataPool() {
        stopSimulation();
        
        if (m_client) {
            m_client->shutdown();
            delete m_client;
            m_client = nullptr;
        }
        
        m_createBtn->setEnabled(true);
        m_destroyBtn->setEnabled(false);
        m_startBtn->setEnabled(false);
        m_stopBtn->setEnabled(false);
        m_yxCountSpin->setEnabled(true);
        m_ycCountSpin->setEnabled(true);
        m_dzCountSpin->setEnabled(true);
        m_ykCountSpin->setEnabled(true);
        
        m_statusLabel->setText("未连接");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        
        m_refreshTimer->stop();
        
        appendLog("数据池已销毁");
    }
    
    void startSimulation() {
        if (!m_client || m_running) return;
        
        m_running = true;
        m_yxChanges = 0;
        m_ycUpdates = 0;
        
        // 创建更新定时器
        m_updateTimer = new QTimer(this);
        connect(m_updateTimer, &QTimer::timeout, this, &CommProcessWindow::updateData);
        m_updateTimer->start(m_intervalSpin->value());
        
        m_startBtn->setEnabled(false);
        m_stopBtn->setEnabled(true);
        
        appendLog("数据采集已启动");
    }
    
    void stopSimulation() {
        if (m_updateTimer) {
            m_updateTimer->stop();
            delete m_updateTimer;
            m_updateTimer = nullptr;
        }
        
        m_running = false;
        m_startBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        
        if (m_client) {
            appendLog("数据采集已停止");
        }
    }
    
    void updateData() {
        if (!m_client || !m_running) return;
        
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<int> yxDist(0, 1);
        static std::uniform_real_distribution<float> ycDist(0.0f, 100.0f);
        
        // 更新YX（发布事件 + 记录SOE）
        int yxChanges = m_yxChangeRate->value();
        for (int i = 0; i < yxChanges && i < (int)m_yxCount; i++) {
            uint32_t idx = rng() % m_yxCount;
            uint8_t oldValue, quality;
            m_client->getYXByIndex(idx, oldValue, quality);
            
            uint8_t newValue = yxDist(rng);
            
            // 使用setYXWithEvent发布变位事件
            m_client->setYXWithEvent(makeKey(1, idx), newValue, 0);
            m_yxEventCount++;
            
            // 记录SOE（仅当值变化时）
            if (oldValue != newValue) {
                m_client->recordSOEYXChange(idx, oldValue, newValue, 128);
                m_soeCount++;
            }
            
            m_yxChanges++;
        }
        
        // 更新YC（发布事件）
        int ycUpdates = m_ycUpdateCount->value();
        for (int i = 0; i < ycUpdates && i < (int)m_ycCount; i++) {
            uint32_t idx = rng() % m_ycCount;
            float value = ycDist(rng);
            
            // 使用setYCWithEvent发布更新事件
            m_client->setYCWithEvent(makeKey(2, idx), value, 0);
            m_ycEventCount++;
            m_ycUpdates++;
        }
        
        // 更新心跳
        m_client->updateHeartbeat();
    }
    
    void refreshData() {
        if (!m_client) return;
        
        // 更新统计标签
        m_yxTotalLabel->setText(QString::number(m_yxCount));
        m_ycTotalLabel->setText(QString::number(m_ycCount));
        m_yxChangesLabel->setText(QString::number(m_yxChanges.load()));
        m_ycUpdatesLabel->setText(QString::number(m_ycUpdates.load()));
        
        // 更新系统监控信息
        updateSystemMonitor();
        
        // 更新YX表格 (显示前30条)
        m_yxTable->setRowCount(std::min(30u, m_yxCount));
        for (uint32_t i = 0; i < std::min(30u, m_yxCount); i++) {
            uint8_t value, quality;
            uint64_t ts;
            m_client->getYXByIndex(i, value, quality);
            ts = 0; // 简化显示
            
            m_yxTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_yxTable->setItem(i, 1, new QTableWidgetItem(QString("1:%1").arg(i)));
            
            QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(value));
            valueItem->setBackground(value ? QBrush(QColor("#C8E6C9")) : QBrush(QColor("#FFCDD2")));
            m_yxTable->setItem(i, 2, valueItem);
            
            m_yxTable->setItem(i, 3, new QTableWidgetItem(QString::number(ts)));
            m_yxTable->setItem(i, 4, new QTableWidgetItem(QString::number(quality)));
        }
        
        // 更新YC表格
        m_ycTable->setRowCount(std::min(30u, m_ycCount));
        for (uint32_t i = 0; i < std::min(30u, m_ycCount); i++) {
            float value;
            uint8_t quality;
            m_client->getYCByIndex(i, value, quality);
            
            m_ycTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_ycTable->setItem(i, 1, new QTableWidgetItem(QString("2:%1").arg(i)));
            
            QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(value, 'f', 2));
            m_ycTable->setItem(i, 2, valueItem);
            
            m_ycTable->setItem(i, 3, new QTableWidgetItem("-"));
            m_ycTable->setItem(i, 4, new QTableWidgetItem(QString::number(quality)));
        }
        
        // 更新进程表格
        SharedDataPool* pool = m_client->getDataPool();
        int row = 0;
        m_procTable->setRowCount(MAX_PROCESS_COUNT);
        for (uint32_t i = 0; i < MAX_PROCESS_COUNT; i++) {
            ProcessInfo info;
            if (pool->getProcessInfo(i, info) == Result::OK && info.active) {
                m_procTable->setItem(row, 0, new QTableWidgetItem(QString::number(i)));
                m_procTable->setItem(row, 1, new QTableWidgetItem(QString::number(info.pid)));
                m_procTable->setItem(row, 2, new QTableWidgetItem(info.name));
                m_procTable->setItem(row, 3, new QTableWidgetItem(QString::number(info.lastHeartbeat)));
                
                QTableWidgetItem* statusItem = new QTableWidgetItem("活跃");
                statusItem->setBackground(QBrush(QColor("#C8E6C9")));
                m_procTable->setItem(row, 4, statusItem);
                row++;
            }
        }
        m_procTable->setRowCount(row);
        
        // 更新YK统计
        m_ykTotalLabel->setText(QString::number(m_ykCountSpin->value()));
        m_ykCmdCountLabel->setText(QString::number(m_ykCmdCount.load()));
        m_ykSuccessLabel->setText(QString::number(m_ykSuccessCount.load()));
        
        // 更新事件统计
        m_yxEventLabel->setText(QString::number(m_yxEventCount.load()));
        m_ycEventLabel->setText(QString::number(m_ycEventCount.load()));
        m_soeCountLabel->setText(QString::number(m_soeCount.load()));
        
        // 处理YK事件
        if (m_ykSubId != INVALID_INDEX) {
            m_client->processEvents(m_ykSubId, 10);
        }
    }
    
    void handleYKEvent(const Event& event) {
        m_ykCmdCount++;
        
        QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        uint64_t key = event.key;
        uint8_t cmdValue = static_cast<uint8_t>(event.newValue.intValue);
        
        appendLog(QString("收到YK命令: Key=%1, 值=%2").arg(key).arg(cmdValue));
        
        // 模拟执行YK命令（这里直接返回成功）
        bool success = true;  // 模拟执行成功
        
        // 设置YK反馈值
        m_client->setYK(key, cmdValue, success ? 0 : 1);
        
        if (success) {
            m_ykSuccessCount++;
            
            // 记录YK执行SOE
            m_client->recordSOEYKExecute(static_cast<uint32_t>(key), cmdValue);
            m_soeCount++;
            
            appendLog(QString("YK执行成功: Key=%1").arg(key));
        } else {
            appendLog(QString("YK执行失败: Key=%1").arg(key));
        }
        
        // 添加到YK表格
        if (m_ykTable->rowCount() >= 50) {
            m_ykTable->removeRow(0);
        }
        int row = m_ykTable->rowCount();
        m_ykTable->insertRow(row);
        m_ykTable->setItem(row, 0, new QTableWidgetItem(timeStr));
        m_ykTable->setItem(row, 1, new QTableWidgetItem(QString::number(key)));
        m_ykTable->setItem(row, 2, new QTableWidgetItem(QString::number(cmdValue)));
        m_ykTable->setItem(row, 3, new QTableWidgetItem(success ? "成功" : "失败"));
        m_ykTable->setItem(row, 4, new QTableWidgetItem(timeStr));
        m_ykTable->setItem(row, 5, new QTableWidgetItem(success ? "已执行" : "执行失败"));
        
        // 添加到事件表
        if (m_eventTable->rowCount() >= 100) {
            m_eventTable->removeRow(0);
        }
        int evtRow = m_eventTable->rowCount();
        m_eventTable->insertRow(evtRow);
        m_eventTable->setItem(evtRow, 0, new QTableWidgetItem(timeStr));
        m_eventTable->setItem(evtRow, 1, new QTableWidgetItem("YK命令"));
        m_eventTable->setItem(evtRow, 2, new QTableWidgetItem(QString::number(key)));
        m_eventTable->setItem(evtRow, 3, new QTableWidgetItem(QString::number(event.oldValue.intValue)));
        m_eventTable->setItem(evtRow, 4, new QTableWidgetItem(QString::number(event.newValue.intValue)));
    }
    
    void updateSystemMonitor() {
        if (!m_processMonitor) return;
        
        // 获取进程资源信息
        ProcessResourceInfo procInfo;
        if (m_processMonitor->getProcessInfo(procInfo)) {
            m_procCpuLabel->setText(QString("%1%").arg(procInfo.cpuPercent, 0, 'f', 1));
            m_procMemLabel->setText(QString::fromStdString(
                ProcessMonitor::formatMemory(procInfo.vmRSS)));
            
            // CPU使用率高时改变颜色
            if (procInfo.cpuPercent > 80) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #F44336;");
            } else if (procInfo.cpuPercent > 50) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
            } else {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
            }
        }
        
        // 获取系统资源信息
        SystemResourceInfo sysInfo;
        if (m_processMonitor->getSystemInfo(sysInfo)) {
            m_sysCpuLabel->setText(QString("%1%").arg(sysInfo.cpuUsage, 0, 'f', 1));
            m_sysMemLabel->setText(QString("%1%").arg(sysInfo.memoryUsage, 0, 'f', 1));
        }
    }
    
    void appendLog(const QString& msg) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_logEdit->append(QString("[%1] %2").arg(timestamp, msg));
    }

private:
    DataPoolClient* m_client;
    ProcessMonitor* m_processMonitor;
    std::atomic<bool> m_running;
    std::atomic<uint64_t> m_yxChanges{0};
    std::atomic<uint64_t> m_ycUpdates{0};
    std::atomic<uint64_t> m_yxEventCount{0};
    std::atomic<uint64_t> m_ycEventCount{0};
    std::atomic<uint64_t> m_soeCount{0};
    std::atomic<uint64_t> m_ykCmdCount{0};
    std::atomic<uint64_t> m_ykSuccessCount{0};
    uint32_t m_yxCount;
    uint32_t m_ycCount;
    uint32_t m_ykSubId = INVALID_INDEX;
    
    // UI组件
    QSpinBox* m_yxCountSpin;
    QSpinBox* m_ycCountSpin;
    QSpinBox* m_dzCountSpin;
    QSpinBox* m_ykCountSpin;
    QSpinBox* m_intervalSpin;
    QSpinBox* m_yxChangeRate;
    QSpinBox* m_ycUpdateCount;
    
    QPushButton* m_createBtn;
    QPushButton* m_destroyBtn;
    QPushButton* m_startBtn;
    QPushButton* m_stopBtn;
    
    QLabel* m_yxTotalLabel;
    QLabel* m_ycTotalLabel;
    QLabel* m_yxChangesLabel;
    QLabel* m_ycUpdatesLabel;
    QLabel* m_statusLabel;
    QLabel* m_ykTotalLabel;
    QLabel* m_ykCmdCountLabel;
    QLabel* m_ykSuccessLabel;
    QLabel* m_yxEventLabel;
    QLabel* m_ycEventLabel;
    QLabel* m_soeCountLabel;
    
    // 系统监控标签
    QLabel* m_procCpuLabel;
    QLabel* m_procMemLabel;
    QLabel* m_sysCpuLabel;
    QLabel* m_sysMemLabel;
    
    QTableWidget* m_yxTable;
    QTableWidget* m_ycTable;
    QTableWidget* m_procTable;
    QTableWidget* m_ykTable;
    QTableWidget* m_eventTable;
    QTextEdit* m_logEdit;
    
    QTimer* m_refreshTimer;
    QTimer* m_updateTimer;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("comm_process");
    QApplication::setApplicationVersion("1.0");
    QApplication::setStyle("Fusion");
    
    CommProcessWindow window;
    window.show();
    
    return app.exec();
}

#include "main.moc"
