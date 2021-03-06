
#include "runer_bike_handler.h"
#include "Logger.h"
#include "bike_service.h"

RunnerEventHandler::RunnerEventHandler(std::shared_ptr<DispatchMsgService> dms, std::shared_ptr<MysqlConnection> conn)
    : dms_(dms), sqlconn_(conn)
{
    dms_->subscribe(EEVENTID_LOCK_REQ, this);
    dms_->subscribe(EEVENTID_UNLOCK_REQ, this);
    dms_->subscribe(EEVENTID_LIST_TRAVELS_REQ, this);
}

RunnerEventHandler::~RunnerEventHandler()
{
    dms_->unsubscribe(EEVENTID_LOCK_REQ, this);
    dms_->unsubscribe(EEVENTID_UNLOCK_REQ, this);
    dms_->unsubscribe(EEVENTID_LIST_TRAVELS_REQ, this);
}

iEvent* RunnerEventHandler::handle(const iEvent* ev)
{
    if (ev == NULL)
    {
        LOG_ERROR("input ev is NULL");
        return NULL;
    }

    u32 eid = ev->get_eid();

    if (eid == EEVENTID_UNLOCK_REQ)
    {
        return handle_unlock_req((UnlockEv*) ev);
    }
    else if (eid == EEVENTID_LOCK_REQ)
    {
        return handle_lock_req((LockEv*) ev);
    }
    else if (eid == EEVENTID_LIST_TRAVELS_REQ)
    {
        return handle_list_travel_records_req((ListTravelRecordsReqEv*) ev);
    }

    return NULL;
}

CommonRspEv* RunnerEventHandler::handle_unlock_req(UnlockEv* ev)
{
    LOG_DEBUG("enter handle_unlock_req");

    std::string mobile = ev->get_mobile();
    std::string bike_code = ev->get_bike_code();

    int bike_code_num = atoi(bike_code.c_str());
    BikeService bs(sqlconn_);
    std::string runner_mobile;
    Bike bk(bike_code_num, mobile);
    if (bs.get_bike(bike_code_num, bk))
    {
        if(bk.st_ == BIKE_ST_UNLOCK)
        {
            if (bk.mobile_.compare(mobile) != 0)
            {
                return new CommonRspEv(ERRO_BIKE_IS_TOOK, "annother one take the bike.", "");
            }
            else
            {
                return new CommonRspEv(ERRO_BIKE_IS_RUNNING, "success", "");
            }
        }
        else if (bk.st_ == BIKE_ST_DAMANGE)
        {
            return new CommonRspEv(ERRO_BIKE_IS_DAMAGED, "annother one take the bike.", "");
        }
        else if (bk.st_ == BIKE_ST_LOCK)   //如果是锁住状态，则解锁
        {
            bk.mobile_ = mobile;    // 设置该单车被mobile用户解锁使用
            if (!bs.unlock(bk))
            {
                return new CommonRspEv(ERRO_PROCCESS_FAILED, "unlock failed.", "");
            }
        }
        else
        {
            // do nothing
        }
    }

    return new CommonRspEv(200, "success", "");
}

LockResultRspEv* RunnerEventHandler::handle_lock_req(LockEv* ev)
{
    std::string mobile = ev->get_mobile();
    std::string bike_code = ev->get_bike_code();
    int bike_code_num = atoi(bike_code.c_str());

    BikeService bs(sqlconn_);
    Bike bk(bike_code_num, mobile);
    if (bs.get_bike(bike_code_num, bk))
    {
        u64 sstmp = bk.unlock_tm_;
        u64 estmp = 0;
        bs.get_current_stmp(estmp);

        LOG_DEBUG("get current stmp = %lld and sstmp=%lld.", estmp, sstmp);

        // 计算里程、排放、卡路里，30公里每小时
        double hour = ((double)(estmp - sstmp)) /3600;
        double mileage   = hour * 30; //* 8.34; // 30公里每小时意为8.34米
        double discharge = mileage * 2; // 以2kg/km计算的
        double calorie   = hour * 469; //骑自行车以469卡路里/60分钟计算的
        std::vector<TravelRecord> record;
        record.push_back(TravelRecord(sstmp, (estmp - sstmp)/60, 1));   // 时长为分钟, 每次一块钱
        TravelInfo trave(mileage, discharge, calorie, record);

        LOG_DEBUG("user (%s) travel : mileage=%.2f, discharge=%.2f, calorie=%.2f", mobile.c_str(), mileage, discharge, calorie);

        if (bk.mobile_.compare(mobile) != 0)
        {
            return new LockResultRspEv(ERRO_BIKE_IS_TOOK, "annother one take the bike.", "", trave);
        }
        else if (bk.st_ == BIKE_ST_UNLOCK)
        {
            /* 同时对多个表格操作，此处这么写有什么问题� 如何保障数据的一致性� */
            if (!bs.lock(bk))
            {
                return new LockResultRspEv(ERRO_PROCCESS_FAILED, "failed", "", trave);
            }
            else
            {
                bs.insert_travel_record(bk.mobile_, bk.type_, mileage, discharge, calorie, sstmp, (estmp - sstmp)/60, 1);
                // TODO 扣钱

            }
        }

        return new LockResultRspEv(ERRC_SUCCESS, "success", "", trave);
    }

    std::vector<TravelRecord> record;
    TravelInfo trave(0, 0, 0, record);
    return new LockResultRspEv(ERRC_INVALID_DATA, "cannot find bike", "", trave);
}

ListTravelRecordsRspEv* RunnerEventHandler::handle_list_travel_records_req(ListTravelRecordsReqEv* ev)
{
    std::string mobile = ev->get_mobile();
    BikeService bs(sqlconn_);

    TravelInfo travel;
    if (!bs.list_travel_records(mobile, travel))
    {
        return new ListTravelRecordsRspEv(ERRO_PROCCESS_FAILED, "failed", "", travel);
    }

    return new ListTravelRecordsRspEv(ERRC_SUCCESS, "success", "", travel);
}


