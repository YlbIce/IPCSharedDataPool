#ifndef DATAPOINTMANAGER_H
#define DATAPOINTMANAGER_H
#include <mutex>
#include <dataitem.h>
#include <QObject>

class DataPointManager : public QObject
{
    Q_OBJECT
public:
    explicit DataPointManager();
    ~DataPointManager();
public:
    static DataPointManager &GetInstance();

    void pushInWaitList(data_ptr data);

    void registerData(int addr, int id);
    void registerData(data_ptr data);

    void onDataItemChanged(data_ptr data);

public slots:
    void onDataItemReturnResult(data_ptr data, WriteState result);

    void onDataPointValueChanged(data_ptr data);
private:
    QList<data_ptr> registerDataList;
    QList<data_ptr> needResultList;
    std::mutex mutex_;

signals:
    void dataItemChanged(data_ptr data);
};

#endif // DATAPOINTMANAGER_H
