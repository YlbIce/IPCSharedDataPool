#ifndef RTDATAPOOL_H
#define RTDATAPOOL_H

#include <dataitem.h>
#include <memory>
#include <vector>
#include <cfg.h>

class RTDataPool_private;
class RTDataPool
{
public:
    RTDataPool();
    ~RTDataPool();


public:
    static RTDataPool *inst();

    std::shared_ptr<DataItem> getDataItem(uint64_t key);
    std::shared_ptr<DataItem> getDataItem(int addr, int id);
    bool InsertDataItem(std::shared_ptr<DataItem> pDataItem);
    bool isExistItem(std::shared_ptr<DataItem> pItem);

    // Web服务扩展方法
    /**
     * @brief 获取所有数据项
     * @return 所有数据项的列表
     */
    std::vector<std::shared_ptr<DataItem>> getAllDataItems();

    /**
     * @brief 根据数据类型获取数据项
     * @param type 数据类型
     * @return 匹配的数据项列表
     */
    std::vector<std::shared_ptr<DataItem>> getDataItemsByType(MetaEnumClass::DATA_TYPE type);

    /**
     * @brief 根据点类型获取数据项
     * @param type 点类型
     * @return 匹配的数据项列表
     */
    std::vector<std::shared_ptr<DataItem>> getDataItemsByPointType(MetaEnumClass::POINT_TYPE type);

    /**
     * @brief 获取分页数据项
     * @param offset 偏移量
     * @param limit 限制数量
     * @return 数据项列表
     */
    std::vector<std::shared_ptr<DataItem>> getPageDataItems(size_t offset, size_t limit);

    /**
     * @brief 获取数据项总数
     * @return 数据项数量
     */
    size_t getDataItemCount();

private:
    static RTDataPool *m_RTDataPool;
    class RTDataPool_private *d_ptr;
    friend class RTDataPool_private; // 添加友元类声明
};
extern RTDataPool* g_RtDataPool;

#endif // RTDATAPOOL_H
