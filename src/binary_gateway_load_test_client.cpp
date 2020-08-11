#include <pthread.h>
#include <thread>
#include <iostream>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
// #include <readline/readline.h>
// #include <readline/history.h>
#include <linenoise.h>
#include <time/h/timestamp.hpp>
#include <latency/h/latency.hpp>
#include <../binary_gateway/h/binary_gateway_interface_messages.hpp>
#include <binary_gateway_load_test_client.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace std;
using namespace rapidjson;

const char *bool_str(bool value)
{
    return (value ? "Yes" : "No");
}

latency x;

binary_gateway_load_test_client::binary_gateway_load_test_client(const std::string &name, const std::string &gateway_client_id, const std::string password, const std::string ip, const std::string port, const std::string root_filename) : generic_framer1(1, 1024), ev_manager(name.c_str(), 1), m_tcpc(1024 * 1024 * 6, 1024 * 1024 * 6)
{
    m_gateway_client_id = gateway_client_id;
    m_name = name;
    m_password = password;
    m_ip = ip;
    m_port = port;
    m_log_filename = root_filename + ".log";
    m_latency_filename = root_filename + ".latency";
    m_main_task_id = -1;
    m_state = task_initialise;
    m_log = new logger(cout, m_log_filename.c_str());
    m_log->set_level(logger::Info);
    m_log->disable_buffered();
    m_log->enable_buffered();
    m_log->enable_async();
    m_log->enable_file();
    m_log->disable_stream();
    m_pfunc[task_initialise] = &binary_gateway_load_test_client::fsm_initialise;
    m_event_delete = true;
    m_con = 0;
    m_tcp_rx_idle_count = 0;
    m_tcp_tx_idle_count = 0;
    m_tcp_connected = false;
    m_idle_count = 0;
    return;
}

binary_gateway_load_test_client::~binary_gateway_load_test_client()
{
    m_log->disable_buffered();
    m_log->flush();
    delete m_log;
    return;
}

int binary_gateway_load_test_client::run()
{
    int ret;

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(2, &mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
        printf("cpu affinity failed\n");
    if ((ret = ev_run()) != E_ev_no_error)
        LOG(*m_log, m_name, logger::Info, "main task failed with error: " << ret)
    else
        LOG(*m_log, m_name, logger::Info, "main task completed successfully")
    m_log->flush();
    return (ret);
}

int binary_gateway_load_test_client::ev_system_event_callback(int event)
{
    ev_event sys_event(event);

    return ((this->*m_pfunc[m_state])(&sys_event));
}

int binary_gateway_load_test_client::ev_app_event_callback(ev_event *pev)
{
    int ret, ret1 = E_ev_no_error;

    m_event_delete = true;
    ret = (this->*m_pfunc[m_state])(pev);
    if (m_event_delete == true)
        ret1 = ev_free(pev);
    return ((ret == E_ev_no_error) ? ret1 : ret);
}

int binary_gateway_load_test_client::fsm_initialise(ev_event *pev)
{
    int ret = E_ev_no_error;

    switch (pev->m_event)
    {
    case EV_ET_STARTUP:
        ret = initialise_task();
        break;
    case ev_idle_tmr:
        ret = process_idle_tmr();
        break;
    case ev_load_tmr:
        ret = process_load_tmr();
        break;
    case ev_load_settings_timer:
        ret = process_load_settings_tmr(ev_load_settings_timer);
    case ev_load_settings_timer_lowops:
        ret = process_load_settings_tmr(ev_load_settings_timer_lowops);
    case ev_load_settings_timer_mediumops:
        ret = process_load_settings_tmr(ev_load_settings_timer_mediumops);
    case ev_load_settings_timer_highops:
        ret = process_load_settings_tmr(ev_load_settings_timer_highops);
        break;
    case ev_random_number_timer:
        ret = process_random_no();
        break;
    case ev_tcp_read:
        ret = tcp_read();
        break;
    case ev_tcp_exception:
        ret = tcp_exception();
        break;
    case ev_write_latency_file:
        ret = write_latency_file();
        break;
    case ev_console_input:
        ret = console_read(pev);
        break;
    default:
        break;
    }
    return (ret);
}
class LoadSettings
{
public:
    LoadSettings()
    {
        m_bUseSop = 0;
        m_LowOps = 0;
        m_LowOpsDuration = 0;
        m_LowOpsInterval = 0;
        m_MediumOps = 0;
        m_MediumOpsDuration = 0;
        m_MediumOpsInterval = 0;
        m_HighOps = 0;
        m_HighOpsDuration = 0;
        m_HighOpsInterval = 0;
        m_nLowTimerState = 0;
        m_nMediumTimerState = 0;
        m_nHighTimerState = 0;
    }
    bool m_bUseSop;
    int m_LowOps;
    int m_LowOpsDuration;
    int m_LowOpsInterval;
    int m_MediumOps;
    int m_MediumOpsDuration;
    int m_MediumOpsInterval;
    int m_HighOps;
    int m_HighOpsDuration;
    int m_HighOpsInterval;
    int m_nLowTimerState;
    int m_nMediumTimerState;
    int m_nHighTimerState;
};
LoadSettings objLoadSettings;
// rancom count
//

int binary_gateway_load_test_client::process_random_no()
{
    m_nRandomNumber = rand() % 100;
    int sign = m_nRandomNumber % 2;

    int tenPercent = m_NumberInBatch / 20;
    if (sign == 0)
        tenPercent = tenPercent * -1;
    m_NumberInBatch += tenPercent;
    //    cout << " m_NumberInBatch " << m_NumberInBatch << endl;
    return E_ev_no_error;
}
int binary_gateway_load_test_client::process_load_settings_tmr(int timerType)
{
    int ret = E_ev_no_error;
    switch (timerType)
    {
    case ev_load_settings_timer_lowops:
    {
        if (objLoadSettings.m_nLowTimerState == 0)
        {
            objLoadSettings.m_nLowTimerState = 1;
            cout << " low ops timer called, ";

            if (objLoadSettings.m_nMediumTimerState == 0 && objLoadSettings.m_nHighTimerState == 0)
                m_NumberInBatch = objLoadSettings.m_LowOps * 1000 / m_TmrIntervalMsecs;

            if ((ret = ev_tmr_add(tmr_load_settings_lowops, ev_load_settings_timer_lowops, objLoadSettings.m_LowOpsDuration * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "duration timer  ok(" << objLoadSettings.m_LowOpsDuration << "),";
                else
                    cout << "duration timer failed, " << ret << ",";
            else
                cout << "duration timer  ok(" << objLoadSettings.m_LowOpsDuration << "),";
        }
        else if (objLoadSettings.m_nLowTimerState == 1)
        {
            cout << " low ops load timer duration elapsed ...." << endl;
            if ((ret = ev_tmr_add(tmr_load_settings_lowops, ev_load_settings_timer_lowops, objLoadSettings.m_LowOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "interval timer   ok(" << objLoadSettings.m_LowOpsInterval << "),";
                else
                    cout << "interval timer  failed, " << ret << ",";
            else
                cout << "interval timer  ok(" << objLoadSettings.m_LowOpsInterval << "),";
        }
        objLoadSettings.m_nLowTimerState = 0;
    }

    break;
    case ev_load_settings_timer_mediumops:
    {

        if (objLoadSettings.m_nMediumTimerState == 0)
        {
            cout << " medium ops timer called,";
            objLoadSettings.m_nMediumTimerState = 1;
            if (objLoadSettings.m_nHighTimerState == 0)
                m_NumberInBatch = objLoadSettings.m_MediumOps * 1000 / m_TmrIntervalMsecs;
            if ((ret = ev_tmr_add(tmr_load_settings_mediumops, ev_load_settings_timer_mediumops, objLoadSettings.m_MediumOpsDuration * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "duration timer  ok(" << objLoadSettings.m_MediumOpsDuration << "),";
                else
                    cout << "duration timer failed, " << ret << ",";
            else
                cout << "duration timer  ok(" << objLoadSettings.m_MediumOpsDuration << "),";
        }
        else if (objLoadSettings.m_nMediumTimerState == 1)
        {
            cout << " Medium load timer duration elapsed,";
            objLoadSettings.m_nMediumTimerState = 0;
            if (objLoadSettings.m_nHighTimerState == 0)
                m_NumberInBatch = objLoadSettings.m_LowOps * 1000 / m_TmrIntervalMsecs;

            if ((ret = ev_tmr_add(tmr_load_settings_mediumops, ev_load_settings_timer_mediumops, objLoadSettings.m_MediumOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "interval timer  ok(" << objLoadSettings.m_MediumOpsInterval << "),";
                else
                    cout << "interval timer failed, " << ret << ",";
            else
                cout << "interval timer  ok(" << objLoadSettings.m_MediumOpsInterval << "),";
        }
    }
    break;
    case ev_load_settings_timer_highops:
    {

        if (objLoadSettings.m_nHighTimerState == 0)
        {
            cout << " high ops timer called,";
            objLoadSettings.m_nHighTimerState = 1;

            m_NumberInBatch = objLoadSettings.m_HighOps * 1000 / m_TmrIntervalMsecs;

            if ((ret = ev_tmr_add(tmr_load_settings_highops, ev_load_settings_timer_highops, objLoadSettings.m_HighOpsDuration * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "duration timer  ok(" << objLoadSettings.m_HighOpsDuration << "),";
                else
                    cout << "duration timer failed, " << ret << ",";
            else
                cout << "duration timer  ok(" << objLoadSettings.m_HighOpsDuration << "),";
        }
        else if (objLoadSettings.m_nHighTimerState == 1)
        {
            cout << " High load timer duration elapsed,";
            objLoadSettings.m_nHighTimerState = 0;
            if (objLoadSettings.m_nMediumTimerState == 1)
                m_NumberInBatch = objLoadSettings.m_MediumOps * 1000 / m_TmrIntervalMsecs;
            else
                m_NumberInBatch = objLoadSettings.m_LowOps * 1000 / m_TmrIntervalMsecs;
            if ((ret = ev_tmr_add(tmr_load_settings_highops, ev_load_settings_timer_highops, objLoadSettings.m_HighOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                if (ret == 1017)
                    cout << "interval timer  ok(" << objLoadSettings.m_HighOpsInterval << "),";
                else
                    cout << "interval timer failed, " << ret << ",";
            else
                cout << "interval timer  ok(" << objLoadSettings.m_HighOpsInterval << "),";
        }
    }
    break;
    default:
        cout << "Timer type not matched " << timerType << endl;
    }
    cout << "Ops " << m_NumberInBatch << endl;
    return E_ev_no_error;
}

int binary_gateway_load_test_client::write_latency_file()
{
    LOG(*m_log, m_name, logger::Info, "start writing latency file")
    m_latency.set_display_range(5);
    m_latency.calc(m_latency_filename, true);
    LOG(*m_log, m_name, logger::Info, "finish writing latency file")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::initialise_task()
{
    int ret;
    srand(time(0));
    //    ev_enable_spinning();
    m_main_task_id = ev_get_task_id();
    LOG(*m_log, m_name, logger::Info, "calibrating RDTSC ticks per usec over 2 seconds")
    m_rdtsc_ticks_per_usec = timestamp::calibrate_rdtsc_ticks_over_usec_period(2000000);
    LOG(*m_log, m_name, logger::Info, "there are " << m_rdtsc_ticks_per_usec << " RDTSC ticks per usec")
    if ((ret = ev_tmr_add(tmr_idle, ev_idle_tmr, 3000000L, ev_epoll::ev_tmr_type_continuous)) != E_ev_no_error)
        LOG(*m_log, m_name, logger::Info, "starting idle tmr failed")
    else
        LOG(*m_log, m_name, logger::Info, "starting idle tmr ok")
    if ((ret = ev_tmr_add(tmr_random_no_timer, ev_random_number_timer, 1000000L, ev_epoll::ev_tmr_type_continuous)) != E_ev_no_error)
        LOG(*m_log, m_name, logger::Info, "starting idle tmr failed")
    else
        LOG(*m_log, m_name, logger::Info, "starting idle tmr ok")

    LOG(*m_log, m_name, logger::Info, "starting load settings  timer")
    if ((ret = ev_tmr_add(tmr_idle, ev_load_settings_timer, 5000000L, ev_epoll::ev_tmr_type_continuous)) != E_ev_no_error)
        LOG(*m_log, m_name, logger::Info, "starting load settings timer failed")
    else
        LOG(*m_log, m_name, logger::Info, "starting load settings ok")
    ret = m_tcpc.set_remote_host(m_ip);
    ret = m_tcpc.set_remote_port(m_port);
    ret = m_tcpc.connect(&m_con);
    if (ret != E_tcp_no_error)
        LOG(*m_log, m_name, logger::Info, "tcp connect failed " << ret)
    else
    {
        LOG(*m_log, m_name, logger::Info, "tcp connect ok IP: " << m_ip << " Port: " << m_port)
        if ((ret = ev_fd_add(m_con->get_fd(), ev_epoll::ev_fd_type_read, ev_tcp_read)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp failed to add read fd on active connection " << ret)
        else if ((ret = ev_fd_add(m_con->get_fd(), ev_epoll::ev_fd_type_except, ev_tcp_exception)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp failed to add exception fd on active connection " << ret)
        else
        {
            LOG(*m_log, m_name, logger::Info, "tcp connection active on fd " << m_con->get_fd())
            m_tcp_connected = true;
            process_a_logon();
        }
    }
    return (ret);
}

using namespace std;
class InstruStats
{

public:
    map<int, int> m_nOrderTypeStats;
    InstruStats()
    {
        m_nOrderNewRequest = 0;
        m_nOrderResponses = 0;
        m_nOrderErrors =0;
    }
    int m_nOrderNewRequest;
    int m_nOrderResponses;
    int m_nOrderErrors;
    void printStats()
    {
        cout << " No of requests " << m_nOrderNewRequest << endl;
        cout << " No of responses " << m_nOrderResponses << endl;
        cout << " No of Error " << m_nOrderErrors << endl;
        for (map<int, int>::iterator l_cIter = m_nOrderTypeStats.begin();
             l_cIter != m_nOrderTypeStats.end(); l_cIter++)
        {
            cout << "  Order type " << l_cIter->first << " Count " << l_cIter->second << endl;
        }
    }
};
class OrderStats
{

public:
    map<int, InstruStats> m_nOrdStats;
    map<int, NEW_ORDER_REQUEST> m_nReq;
    map<int, NEW_ORDER_RESPONSE> m_nRes;
    void printStats(int m_NumberInBatch)
    {
        for (map<int, InstruStats>::iterator l_cIter = m_nOrdStats.begin();
             l_cIter != m_nOrdStats.end(); l_cIter++)
        {
            cout << "orderStats for instutment " << l_cIter->first << endl;
            l_cIter->second.printStats();
            cout << "Current No of ordersper batch " << m_NumberInBatch << endl;
        }
    }
    
    
    void Set(BINARY_INTERFACE_MESSAGES f)
    {
        switch (f.cao_req.MessageType)
        {
        case mt1_new_order_request:
        {

            if (m_nOrdStats.find(f.nos_req.OrderInstrumentID) == m_nOrdStats.end())
            {

                m_nOrdStats.insert(make_pair(f.nos_req.OrderInstrumentID, InstruStats()));
            }

            InstruStats &stat = m_nOrdStats[f.nos_req.OrderInstrumentID];
            stat.m_nOrderNewRequest++;
            if (stat.m_nOrderTypeStats.find(f.nos_req.OrderType) == stat.m_nOrderTypeStats.end())
                stat.m_nOrderTypeStats.insert(make_pair(f.nos_req.OrderType, 0));
            stat.m_nOrderTypeStats[f.nos_req.OrderType]++;
            m_nReq.insert(make_pair(f.nos_req.UserTag, f.nos_req));
        }
        case mt1_new_order_response:
        {

            if (m_nReq.find(f.nos_res.UserTag) != m_nReq.end())
            {

                m_nOrdStats.insert(make_pair(m_nReq[f.nos_res.UserTag].OrderInstrumentID, InstruStats()));
            }

            InstruStats &stat = m_nOrdStats[m_nReq[f.nos_res.UserTag].OrderInstrumentID];
            stat.m_nOrderResponses++;
            if(f.nos_res.StatusID != 0)
                stat.m_nOrderErrors ++;
            m_nRes.insert(make_pair(f.nos_res.UserTag, f.nos_res));
        }

        break;
        default:
        {
            cout << " stat failed for message " << f.cao_req.MessageType;
        }
        }
    }
};
OrderStats objStats;

int binary_gateway_load_test_client::process_idle_tmr()
{
    if (m_tcp_connected == true)
    {
        if (++m_tcp_rx_idle_count > 6)
        {
            LOG(*m_log, m_name, logger::Info, "tcp no rx data seen from binary gateway tcp connection for " << 6 << " seconds")
            ev_terminate();
        }
        else if (++m_tcp_tx_idle_count >= 1)
            send_heartbeat();
    }
    if (++m_idle_count > 10)
    {
        m_log->flush();
        m_idle_count = 0;
    }
    return (E_ev_no_error);
}

// 1,2,1, 1000000,1,25052,15000,35000,1000,1,1,3500000,700,2,100000000,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,25052,25052,0,0
// 2,2,1, 100,1,700000,500000,900000,1000000,1,10,100000000,700,1,10000000000,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,700000,700000,0,0
// 3,2,1, 100,1,16962,10962,26962,100000,1,1,350000000,150,2,100000000,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,16962,16962,0,0
// 4,2,1, 10000000000,1,1038,438,2038,1,1,1,100000,80,3,100,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,1038,1038,0,0
// 6,2,1, 1000000,1,721,421,1021,100,1,1,10000000,2800,3,100,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,721,721,0,0
// 10,2,1,1000000,1,5927,3927,7927,100,1,1,10000000,100,7,100000000,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,5927,5927,0,0
// 11,2,1,100,1,3890,2000,9000,100000,1,1,10000000000,100,7,100000000,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,3890,3890,0,0
// 12,2,1,1000000,1,32216,12216,52216,1000,1,1,100000000,300,8,100000000,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,32216,32216,0,0
// 13,2,1,100,1,21846,11846,31846,100000,1,1,10000000000,300,8,100000000,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,21846,21846,0,0
// 14,2,1,1000000,1,10865,4865,24865,1000,1,1,100000000,300,9,1000000,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,10865,10865,0,0
// 15,2,1,100,1,7341,3200,12000,100000,1,1,10000000000,300,9,1000000,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,7341,7341,0,0
// 16,2,1,100000000,1,2694,1394,3894,1,1,1,100000,300,10,100,1,10000000000,1,10000000000,10,8,0,0,0,0,0,0,0,0,2694,2694,0,0
// 17,2,1,100000,1,18330,17330,19330,10,1,1,1000000,300,10,100,4,10000,1,10000000000,10,8,0,0,0,0,0,0,0,0,18330,18330,0,0
// 18,2,2,10,5,70000,10000,250000,1,1,1,1000000,700,1,10000000000,11,100,1,10000000000,10,5,100,100,10000,50,1500000000000,1500000000000,1000,1000,68890,70000,1,20
map<int, pair<int, int>> m_instruments;

int binary_gateway_load_test_client::process_load_tmr()
{
    m_instruments.insert(make_pair(1, make_pair(500100, 3500000)));
    m_instruments.insert(make_pair(2, make_pair(400, 500)));
    m_instruments.insert(make_pair(3, make_pair(10962, 26962)));
    m_instruments.insert(make_pair(4, make_pair(438, 1038)));
    m_instruments.insert(make_pair(6, make_pair(421, 1021)));
    m_instruments.insert(make_pair(10, make_pair(3927, 7927)));
    m_instruments.insert(make_pair(11, make_pair(2000, 9000)));
    m_instruments.insert(make_pair(12, make_pair(12216, 52216)));
    m_instruments.insert(make_pair(13, make_pair(11846, 31846)));
    m_instruments.insert(make_pair(14, make_pair(4865, 24865)));
    m_instruments.insert(make_pair(15, make_pair(12000, 100000)));
    m_instruments.insert(make_pair(16, make_pair(1394, 3894)));
    m_instruments.insert(make_pair(17, make_pair(17330, 19330)));
    m_instruments.insert(make_pair(18, make_pair(7330, 19330)));
    BINARY_FRAME f;
    int ret = E_ev_no_error, count = 0;
    LOG(*m_log, m_name, logger::Info, "process load tmr")

    while (ret == E_ev_no_error && count < m_NumberInBatch && m_NumberToSend > 0)
    {
        f.hdr.msg_len = sizeof(NEW_ORDER_REQUEST);
        f.msg.nos_req.MessageType = mt1_new_order_request;
        f.msg.nos_req.UserID = (count % m_NumberUsers) + m_StartUserID;
        f.msg.nos_req.UserTag = m_UserTag;
        // f.msg.nos_req.AccountType = 2;
        // memset(f.msg.nos_req.AccountID1, 0, sizeof(f.msg.nos_req.AccountID1));
        // memset(f.msg.nos_req.AccountID2, 0, sizeof(f.msg.nos_req.AccountID2));
        f.msg.nos_req.OrderInstrumentID = (m_counter % 11) + 1; /*18*/
        ;
        f.msg.nos_req.OrderType = (m_counter % 9) + 1;
        f.msg.nos_req.OrderSide = (m_counter % 2) + 1;
        f.msg.nos_req.OrderQuantity = 100 /*1000*/;
        f.msg.nos_req.OrderDisclosedQuantity = 100 /*1000*/;
        f.msg.nos_req.TriggerOn = /*(m_counter % 1)+1*/ 1;
        int price = m_instruments[f.msg.nos_req.OrderInstrumentID].first;
        f.msg.nos_req.OrderPrice = /*f.msg.nos_req.OrderType ==1 ? 0:price;*/ ((f.msg.nos_req.OrderType == 1) || (f.msg.nos_req.OrderType == 4) || (f.msg.nos_req.OrderType == 7) || (f.msg.nos_req.OrderType == 9)) ? 0 : price; /*(400 + (f.msg.nos_req.OrderSide == 2 ? (m_counter+100)+10 : (m_counter%100)-5));*/
        f.msg.nos_req.OrderTriggerPrice = price + ((f.msg.nos_req.OrderSide == 2 || f.msg.nos_req.UserID % 3) ? 5 : 0);
        f.msg.nos_req.OrderTimeInForce = (m_counter % 4) + 1;
        f.msg.nos_req.MarketPriceProtectionTicks = 0;
        f.msg.nos_req.OrderExecutionType = 1;

        LOG(*m_log, m_name, logger::Info, "j { " << '"' << "MessageLength" << '"' << " : "
                                                 << "65"
                                                 << ", " << '"' << "MessageType" << '"' << " : "
                                                 << "3"
                                                 << ", " << '"' << "UserID" << '"' << " : " << f.msg.nos_req.UserID << ", " << '"' << "UserTag" << '"' << " : " << f.msg.nos_req.UserTag << ", "

                                                 << '"' << "InstrumentID" << '"' << " : " << f.msg.nos_req.OrderInstrumentID << ", " << '"' << "OrderType" << '"' << " : " << (int)f.msg.nos_req.OrderType << ", " << '"' << "TIF" << '"' << " : " << (int)f.msg.nos_req.OrderTimeInForce << ", " << '"' << "Side" << '"' << " : " << (int)f.msg.nos_req.OrderSide << ", " << '"' << "ExType" << '"' << " : " << (int)f.msg.nos_req.OrderExecutionType << ", "

                                                 << '"' << "Quantity" << '"' << " : " << f.msg.nos_req.OrderQuantity << ", " << '"' << "DisclosedQuantity" << '"' << " : " << f.msg.nos_req.OrderDisclosedQuantity << ", " << '"' << "Price" << '"' << " : " << f.msg.nos_req.OrderPrice << ", "

                                                 << '"' << "MarketPriceProtectionTicks" << '"' << " : " << f.msg.nos_req.MarketPriceProtectionTicks << ", " << '"' << "TriggerPrice" << '"' << " : " << f.msg.nos_req.OrderTriggerPrice << ", " << '"' << "OrdTrig" << '"' << " : " << (int)f.msg.nos_req.TriggerOn << " }");

        // typedef struct
        //     {
        //     unsigned int                    MessageType;
        //     unsigned long long              GatewayTag;
        //     unsigned long long              RequestUserID;
        //     unsigned long long              RequestUserTag;
        //     unsigned long long              UserID;
        //     unsigned long long              OrderInstrumentID;
        //     unsigned long  long             OrderSide;
        //     unsigned long long              OrderType;
        //     unsigned long long              OrderTimeInForce;
        //     unsigned long long              OrderExecutionType;             //PostOnly, CloseOnTrigger, ReduceOnly
        //     unsigned long long              OrderTotalQuantity;
        //     unsigned long long              OrderDisclosedQuantity;
        //     long long                       OrderPrice;
        //     long long                       MarketPriceProtectionTicks;     // MDW check with paul this is right
        //     long long                       OrderTriggerPrice;
        //     unsigned long long              TriggerOn;
        //     unsigned long long              ExchangeOrderID;
        //     unsigned long long              ExchangeEventID;
        //     unsigned long long              ExchangeStatusID;
        //     unsigned long  long             TimeStamp;

        //     } ME_NEW_ORDER_REQUESTRESPONSE;
        typedef struct
        {
            unsigned int MessageType;
            unsigned long long GatewayTag;
            unsigned long long RequestUserID;
            unsigned long long RequestUserTag;
            unsigned long long UserID;
            unsigned long long OrderInstrumentID;
            unsigned long long OrderSide;
            unsigned long long OrderType;
            unsigned long long OrderTimeInForce;
            unsigned long long OrderExecutionType; //PostOnly, CloseOnTrigger, ReduceOnly
            unsigned long long OrderTotalQuantity;
            unsigned long long OrderDisclosedQuantity;
            long long OrderPrice;
            long long MarketPriceProtectionTicks; // MDW check with paul this is right
            long long OrderTriggerPrice;
            unsigned long long TriggerOn; //LastTradedPrice, MarkPrice
            unsigned long long ExchangeOrderID;
            unsigned long long ExchangeEventID;
            unsigned long long ExchangeStatusID;
            unsigned long long TimeStamp;

        } ME_NEW_ORDER_REQUESTRESPONSE;

        LOG(*m_log, m_name, logger::Info, "m { " << '"' << "MessageLength" << '"' << " : "
                                                 << "160"
                                                 << ", " << '"' << "MessageType" << '"' << " : "
                                                 << "21"
                                                 << ", " << '"' << "GatewayTag" << '"' << " : " << 0 << ", " << '"' << "RequestUserID" << '"' << " : " << 0 << ", " << '"' << "RequestUserTag" << '"' << " : " << 0 << ", " << '"' << "UserID" << '"' << " : " << f.msg.nos_req.UserID << ", "
                                                 //<<'"'<<"UserTag"<<'"'<<" : "<<f.msg.nos_req.UserTag<<", "
                                                 << '"' << "InstrumentID" << '"' << " : " << f.msg.nos_req.OrderInstrumentID << ", " << '"' << "SideM" << '"' << " : " << (int)f.msg.nos_req.OrderSide << ", " << '"' << "OrderTypeM" << '"' << " : " << (int)f.msg.nos_req.OrderType << ", " << '"' << "TIFM" << '"' << " : " << (int)f.msg.nos_req.OrderTimeInForce << ", "

                                                 << '"' << "ExTypeM" << '"' << " : " << (int)f.msg.nos_req.OrderExecutionType << ", "

                                                 << '"' << "Quantity" << '"' << " : " << f.msg.nos_req.OrderQuantity << ", " << '"' << "DisclosedQuantity" << '"' << " : " << f.msg.nos_req.OrderDisclosedQuantity << ", " << '"' << "Price" << '"' << " : " << f.msg.nos_req.OrderPrice << ", "

                                                 << '"' << "MarketPriceProtectionTicks" << '"' << " : " << f.msg.nos_req.MarketPriceProtectionTicks << ", " << '"' << "TriggerPrice" << '"' << " : " << f.msg.nos_req.OrderTriggerPrice << ", " << '"' << "OrdTrigM" << '"' << " : " << (int)f.msg.nos_req.TriggerOn << ", " << '"' << "ExchangeOrderID" << '"' << " : " << 0 << ", " << '"' << "ExchangeEventID" << '"' << " : " << 0 << ", " << '"' << "ExchangeStatusID" << '"' << " : " << 0 << ", " << '"' << "TimeStamp" << '"' << " : " << 0 << " }");

        if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(NEW_ORDER_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed stop test")
        else
        {
            objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                ++m_counter;
                ++count;
                --m_NumberToSend;
                ++m_UserTag;
            }
        }
    }
    //sleep(2000);

    if (ret == 0 && m_NumberToSend > 0 && (ret = ev_tmr_add(tmr_load, ev_load_tmr, m_TmrIntervalMsecs * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
        LOG(*m_log, m_name, logger::Info, "starting load tmr failed")
    if (m_NumberToSend == 0)
    {
        LOG(*m_log, m_name, logger::Info, "test complete")
        m_counter = 0;
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::check_set()
{
    LOG(*m_log, m_name, logger::Info, "check set, the set has " << m_user_tags.size() << " items in it")
    for (auto &p : m_user_tags)
        LOG(*m_log, m_name, logger::Info, "set tag " << p)
    m_user_tags.clear();
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::tcp_read()
{
    int ret;
    int len;
    char buf[32768];

    if ((ret = m_con->read_data(buf, sizeof(buf), len)) == E_tcp_no_error)
    {
        m_tcp_rx_idle_count = 0;
        if ((ret = parse_raw_rx_data(0, buf, len)) != E_frmr_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp process raw data has failed, error " << ret)
    }
    else if (ret == E_tcp_would_block)
        ret = E_tcp_no_error;
    else
        LOG(*m_log, m_name, logger::Info, "tcp read has failed, error " << ret)
    if (ret != E_tcp_no_error)
        ret = ev_terminate();
    return (ret);
}

int binary_gateway_load_test_client::process_rx_frame(unsigned int index, const char *src, unsigned int len)
{
    int ret = E_ev_no_error;
    BINARY_INTERFACE_MESSAGES *m;

    if (len < 4)
        LOG(*m_log, m_name, logger::Info, "rx frame must be at least 4 characters long to form a proper message, dump" << hexdump(src, len, 32))
    else
    {
        src += sizeof(FRAME_HDR);
        len -= sizeof(FRAME_HDR);
        m = (BINARY_INTERFACE_MESSAGES *)src;
        switch (m->msg_type)
        {
        case mt1_server_heartbeat:
            //LOG(*m_log, m_name, logger::Info, "rx heartbeat ok")
            break;
        case mt1_gateway_client_logon_response:
            LOG(*m_log, m_name, logger::Info, "rx gateway client logon response ok StatusID(" << m->gclg_res.StatusID << ") Timestamp(" << m->gclg_res.TimeStamp << ")")
            break;
        case mt1_gateway_client_logoff_response:
            LOG(*m_log, m_name, logger::Info, "rx gateway client logoff response ok status " << m->gclo_res.StatusID)
            break;
        case mt1_new_order_response:
            objStats.Set(*m);
            ret = process_new_order_single_response(m->nos_res);
            break;
        case mt1_modify_order_response:
            ret = process_modify_order_response(m->mo_res);
            break;
        case mt1_cancel_order_response:
            ret = process_cancel_order_response(m->co_res);
            break;
        case mt1_cancel_all_orders_response:
            ret = process_cancel_all_orders_response(m->cao_res);
            break;
        // case mt1_cancel_replace_order_response:
        //     ret = process_cancel_replace_order_response(m->cro_res);
        //     break;
        case mt1_working_orders_response:;
            ret = process_working_orders_response(m->wo_res);
            break;
        case mt1_fill_response:
            ret = process_fill_response(m->fill_res);
            break;
        case mt1_working_order_notification:
            ret = process_working_order_notification(m->wo_not);
            break;
        case mt1_modify_order_notification:
            ret = process_modify_order_notification(m->mo_not);
            break;
        case mt1_cancel_order_notification:
            ret = process_cancel_order_notification(m->co_not);
            break;
        case mt1_fill_notification:
            ret = process_fill_notification(m->fil_not);
            break;
        case mt1_session_notification:
            ret = process_session_notification(m->ses_not);
            break;
        case mt1_stop_trigger_notification:
            //ret = process_stop_trigger_notification(m->st_not);
            break;
        case mt1_gateway_client_logoff_notification:
            ret = process_gateway_client_logoff_notification(m->gclo_not);
            break;
        default:
            LOG(*m_log, m_name, logger::Info, "rx unknown message " << hexdump(src, len, 32))
            break;
        }
    }
    return (ret);
}

int binary_gateway_load_test_client::tcp_write(char *src, int len)
{
    int ret, tx_len;

    if ((ret = m_con->write_data(src, len, tx_len)) != E_tcp_no_error)
    {
        LOG(*m_log, m_name, logger::Info, "tcp write to binary gateway failed ret " << ret)
        ret = ev_terminate();
    }
    else if (tx_len != len)
    {
        LOG(*m_log, m_name, logger::Info, "tcp write to binary gateway failed, attempted " << len << " bytes, only wrote " << tx_len << " bytes")
        ret = ev_terminate();
    }
    else
    {
        m_tcp_tx_idle_count = 0;
        ret = E_ev_no_error;
    }
    return (ret);
}

int binary_gateway_load_test_client::tcp_exception()
{
    LOG(*m_log, m_name, logger::Info, "tcp exception on tcp client listen connection")
    return (0);
}

int binary_gateway_load_test_client::p_set_flags(int fd, int flags)
{
    int ret, val;

    if ((val = fcntl(fd, F_GETFL, 0)) < 0)
        ret = E_ev_get_flags;
    else if (val |= flags, fcntl(fd, F_SETFL, val) < 0)
        ret = E_ev_set_flags;
    else
        ret = E_ev_no_error;
    return (ret);
}

int binary_gateway_load_test_client::process_json_structures(char *buf)
{

    char tmp[2048] = {0};
    char writebuf[2048] = {0};
    // ESG_FRAME f = {0};
    int nOffSet = 0;
    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("MessageType { \"Data1\" : 1, \"Data2\" : 1...... }\n>> ");
        fflush(stdout);
    }
    else
    {
        Document doc;
        //static const char *kTypeNames[] = {"Null", "False", "True", "Object", "Array", "String", "Number"};
        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            cout << "\nDocument scan" << endl;

            for (Value::ConstMemberIterator itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr)
            {
                //printf("Member [%s] - type is [%s]\n", itr->name.GetString(), kTypeNames[itr->value.GetType()]);

                if (strcmp(itr->name.GetString(), "MessageLength") == 0)
                {
                    unsigned int nVal = itr->value.GetUint();
                    memcpy(writebuf + nOffSet, &nVal, 2);
                    nOffSet += 2;
                }
                else if (strcmp(itr->name.GetString(), "MessageType") == 0)
                {
                    unsigned int nVal = itr->value.GetUint();
                    memcpy(writebuf + nOffSet, &nVal, 2);
                    nOffSet += 2;
                }
                else if (strcmp(itr->name.GetString(), "MarketPriceProtectionTicks") == 0)
                {
                    unsigned int nVal = itr->value.GetUint();
                    memcpy(writebuf + nOffSet, &nVal, 2);
                    nOffSet += 2;
                }
                else if (
                    strcmp(itr->name.GetString(), "OrderType") == 0 ||
                    strcmp(itr->name.GetString(), "TIF") == 0 ||
                    strcmp(itr->name.GetString(), "Side") == 0 ||
                    strcmp(itr->name.GetString(), "ExType") == 0 ||
                    strcmp(itr->name.GetString(), "OrdTrig") == 0)
                {
                    //                  LOG(*m_log, m_name, logger::Info, "j { "
                    // <<'"'<<"MessageType"<<'"'<<" : "<<"3"<<", "
                    // <<'"'<<"UserID"<<'"'<<" : "<<f.msg.nos_req.UserID<<", "
                    // <<'"'<<"UserTag"<<'"'<<" : "<<f.msg.nos_req.UserTag<<", "

                    // <<'"'<<"InstrumentID"<<'"'<<" : "<<f.msg.nos_req.OrderInstrumentID<<", "
                    // <<'"'<<"OrderType"<<'"'<<" : "<< (int)f.msg.nos_req.OrderType<<", "
                    // <<'"'<<"TIF"<<'"'<<" : "<<(int)f.msg.nos_req.OrderTimeInForce<<", "
                    // <<'"'<<"Side"<<'"'<<" : "<<(int)f.msg.nos_req.OrderSide<<", "
                    // <<'"'<<"ExType"<<'"'<<" : "<<(int)f.msg.nos_req.OrderExecutionType<<", "

                    // <<'"'<<"Quantity"<<'"'<<" : "<<f.msg.nos_req.OrderQuantity<<", "
                    // <<'"'<<"DisclosedQuantity"<<'"'<<" : "<<f.msg.nos_req.OrderDisclosedQuantity<<", "
                    // <<'"'<<"Price"<<'"'<<" : "<<f.msg.nos_req.OrderPrice<<", "

                    // <<'"'<<"MarketPriceProtectionTicks"<<'"'<<" : "<<f.msg.nos_req.MarketPriceProtectionTicks
                    // <<'"'<<"TriggerPrice"<<'"'<<" : "<<f.msg.nos_req.OrderTriggerPrice<<", "
                    // <<'"'<<"OrdTrig"<<'"'<<" : "<<(int)f.msg.nos_req.TriggerOn <<", "
                    // <<" }");
                    unsigned int nVal = itr->value.GetUint();
                    memcpy(writebuf + nOffSet, &nVal, 1);
                    nOffSet += 1;
                }
                else if (strcmp(itr->name.GetString(), "Password") == 0 ||
                         strcmp(itr->name.GetString(), "PrimaryIPAddress") == 0 ||
                         strcmp(itr->name.GetString(), "SecondaryIPAddress") == 0 ||
                         strcmp(itr->name.GetString(), "PasswordDetails") == 0)
                {
                    const char *nVal = itr->value.GetString();
                    int nStringLength = itr->value.GetStringLength();
                    //memcpy(writebuf+nOffSet,0,16);
                    strncpy(writebuf + nOffSet, nVal, nStringLength);
                    nOffSet += 16;
                }
                else
                {
                    unsigned long long nVal = itr->value.GetUint64();
                    memcpy(writebuf + nOffSet, &nVal, 8);
                    nOffSet += 8;
                }

                //cout <<"Value is " << nVal<<endl;

                // cout << "\n"
                //      << endl;
            }
        }
        cout << "writiing MessageType " << *((int *)(writebuf + 4)) << " Bytes " << nOffSet;
        LOG(*m_log, m_name, logger::Info, "writiing MessageType " << *((int *)(writebuf + 4)) << " Bytes " << nOffSet)
        int ret = tcp_write(writebuf, nOffSet);
        if (ret != E_ev_no_error)
        {
            LOG(*m_log, m_name, logger::Info, "tx writing json message to me failed " << ret)
        }
        return (E_ev_no_error);
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::console_read(ev_event *pev)
{
    console_input *i = (console_input *)pev;
    int ret = E_ev_no_error;

    LOG(*m_log, m_name, logger::Info, "console command (" << i->buf << ")")
    if (!strncasecmp(i->buf, "Q", 1))
    {
        LOG(*m_log, m_name, logger::Info, "test harness requested to terminate")
        ret = ev_terminate();
    }
    else if (!strncasecmp(i->buf, "L", 1))
    {
        ret = process_a_logoff(i->buf);
    }
    else if (!strncasecmp(i->buf, "N", 1))
    {
        ret = process_a_new_order(i->buf);
    }
    else if (!strncasecmp(i->buf, "M", 1))
    {
        ret = process_a_modify_order(i->buf);
    }
    else if (!strncasecmp(i->buf, "C", 1))
    {
        ret = process_a_cancel_order(i->buf);
    }
    else if (!strncasecmp(i->buf, "A", 1))
    {
        ret = process_a_cancel_all_orders(i->buf);
    }
    else if (!strncasecmp(i->buf, "R", 1))
    {
        ret = process_a_cancel_replace(i->buf);
    }
    else if (!strncasecmp(i->buf, "W", 1))
    {
        ret = process_working_orders(i->buf);
    }
    else if (!strncasecmp(i->buf, "F", 1))
    {
        ret = process_get_fills(i->buf);
    }
    else if (!strncasecmp(i->buf, "B", 1))
    {
        ret = process_a_new_order_bulk(i->buf);
    }
    else if (!strncasecmp(i->buf, "Y", 1))
    {
        ret = do_latency();
    }
    else if (!strncasecmp(i->buf, "V", 1))
    {
        ret = check_set();
    }
    else if (!strncasecmp(i->buf, "j", 1))
    {

        ret = process_json_structures(i->buf);
    }
    else if (!strncasecmp(i->buf, "S", 1))
    {

        objStats.printStats(m_NumberInBatch);
    }
    else if (!strncasecmp(i->buf, "?", 1))
    {
        ret = print_menu();
    }
    else
    {
        LOG(*m_log, m_name, logger::Info, "unknown command, ignored")
    }
    return (ret);
}

int binary_gateway_load_test_client::print_menu()
{
    printf("Cmnds\n");
    printf("A  cancel all orders\n");
    printf("C  cancel an order\n");
    printf("F  get all fills\n");
    printf("L  logoff\n");
    printf("M  modify an order\n");
    printf("N  new order\n");
    printf("R  cancel replace an order\n");
    printf("W  working orders\n");
    printf("S  print stats\n");
    printf("B  bulk orders\n");
    printf("?  this menu\n");
    fflush(stdout);
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::do_latency()
{
    x.calc("client_latency.txt", true);
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::send_heartbeat()
{
    BINARY_FRAME f;

    f.hdr.msg_len = sizeof(HEARTBEAT);
    f.msg.nos_req.MessageType = mt1_client_heartbeat;
    if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(HEARTBEAT)) != 0)
        LOG(*m_log, m_name, logger::Info, "tx hearbeat failed")
    else
        //  LOG(*m_log, m_name, logger::Info, "tx heartbeat ok")
        return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_logon()
{
    BINARY_FRAME f;

    f.hdr.msg_len = sizeof(GATEWAY_CLIENT_LOGON_REQUEST);
    f.msg.gclg_req.MessageType = mt1_gateway_client_logon_request;
    f.msg.gclg_req.GatewayClientID = std::stoi(m_gateway_client_id);
    memset(f.msg.gclg_req.Password, 0, sizeof(f.msg.gclg_req.Password));
    memcpy(f.msg.gclg_req.Password, m_password.data(), m_password.length());
    if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(GATEWAY_CLIENT_LOGON_REQUEST)) != 0)
        LOG(*m_log, m_name, logger::Info, "tx gateway client logon request failed")
    else
        LOG(*m_log, m_name, logger::Info, "tx gateway client logon request ok")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_logoff(char *buf)
{
    BINARY_FRAME f;

    f.hdr.msg_len = sizeof(GATEWAY_CLIENT_LOGOFF_REQUEST);
    f.msg.nos_req.MessageType = mt1_gateway_client_logoff_request;
    if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(GATEWAY_CLIENT_LOGOFF_REQUEST)) != 0)
        LOG(*m_log, m_name, logger::Info, "tx gateway client logoff request failed")
    else
        LOG(*m_log, m_name, logger::Info, "tx gateway client logoff request ok")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_set_load_setting(char *buf)
{

    int ret;
    // char tmp[512];

    // while (*buf && *buf != '{')
    //     ++buf;
    // if (*buf == 0)
    //     {
    //     printf("V { \"LowOps\" : 1, \"LowOpsDuration(minutes) \" : 1, \"MediumOps \" : 1, \"MediumOpsDuration(minutes) \" : 1, \"HighOps\" : 10, \"HighOpsDuration(minnutes) \" : 1 }\n>> ");
    //     fflush(stdout);
    //     }
    // else
    //     {
    //     Document doc;

    //     strcpy(tmp, buf);
    //     if (doc.ParseInsitu(tmp).HasParseError())
    //         LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
    //     else
    //         {

    //         m_counter = 0;
    //         if ((ret = ev_tmr_add(tmr_load, ev_load_tmr, m_TmrIntervalMsecs * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
    //             LOG(*m_log, m_name, logger::Info, "starting load tmr failed")
    //         else
    //             LOG(*m_log, m_name, logger::Info, "starting load tmr ok")
    //         }
    //     }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_new_order(char *buf)
{
    int ret;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("N { \"NumberToSend\" : 100000000, \"NumberInBatch\" : 1000, \"TmrIntervalMsecs\" : 1000, \"NumberUsers\" : 1, \"StartUserID\" : 1, \"UserTag\" : 1, \"UseSOP\" : 1, \"MediumOps\" : 5000, \"MediumOpsDuration\" : 5000, \"MediumOpsInterval\" : 30000, \"HighOps\" : 50000, \"HighOpsDuration\" : 120000, \"HighOpsInterval\" : 360000 }\n>> ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            if (doc.HasMember("NumberToSend"))
                m_NumberToSend = doc["NumberToSend"].GetInt();
            else
                m_NumberToSend = 1;
            if (doc.HasMember("NumberInBatch"))
                m_NumberInBatch = doc["NumberInBatch"].GetInt();
            else
                m_NumberInBatch = 1;
            if (doc.HasMember("TmrIntervalMsecs"))
                m_TmrIntervalMsecs = doc["TmrIntervalMsecs"].GetInt();
            else
                m_TmrIntervalMsecs = 1000;
            if (doc.HasMember("NumberUsers"))
                m_NumberUsers = doc["NumberUsers"].GetInt();
            else
                m_NumberUsers = 1;
            if (doc.HasMember("StartUserID"))
                m_StartUserID = doc["StartUserID"].GetInt();
            else
                m_StartUserID = 1;

            if (doc.HasMember("UserTag"))
                m_UserTag = doc["UserTag"].GetInt();
            else
                m_UserTag = 1;

            if (doc.HasMember("UseSOP"))
                objLoadSettings.m_bUseSop = doc["UseSOP"].GetInt();
            else
                objLoadSettings.m_bUseSop = 1;

            if (doc.HasMember("LowOps"))
                objLoadSettings.m_LowOps = doc["LowOps"].GetInt();
            else
                objLoadSettings.m_LowOps = m_NumberInBatch;

            if (doc.HasMember("LowOpsDuration"))
                objLoadSettings.m_LowOpsDuration = doc["LowOpsDuration"].GetInt();
            else
                objLoadSettings.m_LowOpsDuration = 24 * 60 * 60 * 1000;

            if (doc.HasMember("LowOpsInterval"))
                objLoadSettings.m_LowOpsInterval = doc["LowOpsInterval"].GetInt();
            else
                objLoadSettings.m_LowOpsInterval = 24 * 60 * 60 * 1000;

            if (doc.HasMember("MediumOps"))
                objLoadSettings.m_MediumOps = doc["MediumOps"].GetInt();
            else
                objLoadSettings.m_MediumOps = 50000;

            if (doc.HasMember("MediumOpsDuration"))
                objLoadSettings.m_MediumOpsDuration = doc["MediumOpsDuration"].GetInt();
            else
                objLoadSettings.m_MediumOpsDuration = 30;

            if (doc.HasMember("MediumOpsInterval"))
                objLoadSettings.m_MediumOpsInterval = doc["MediumOpsInterval"].GetInt();
            else
                objLoadSettings.m_MediumOpsInterval = 30;

            if (doc.HasMember("HighOps"))
                objLoadSettings.m_HighOps = doc["HighOps"].GetInt();
            else
                objLoadSettings.m_HighOps = 200000;

            if (doc.HasMember("HighOpsDuration"))
                objLoadSettings.m_HighOpsDuration = doc["HighOpsDuration"].GetInt();
            else
                objLoadSettings.m_HighOpsDuration = 15;

            if (doc.HasMember("HighOpsInterval"))
                objLoadSettings.m_HighOpsInterval = doc["HighOpsInterval"].GetInt();
            else
                objLoadSettings.m_HighOpsInterval = 15;
            if (objLoadSettings.m_bUseSop == 1)
            {
                if (objLoadSettings.m_LowOpsInterval > 0)
                {
                    if ((ret = ev_tmr_add(tmr_load_settings_lowops, ev_load_settings_timer_lowops, objLoadSettings.m_LowOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                        LOG(*m_log, m_name, logger::Info, "starting low ops timer failed " << ret)
                    else
                        LOG(*m_log, m_name, logger::Info, "starting low ops timer ok")
                }
                if (objLoadSettings.m_MediumOpsInterval > 0)
                {
                    if ((ret = ev_tmr_add(tmr_load_settings_mediumops, ev_load_settings_timer_mediumops, objLoadSettings.m_MediumOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                        LOG(*m_log, m_name, logger::Info, "starting medium ops tmr failed " << ret)
                    else
                        LOG(*m_log, m_name, logger::Info, "starting medium ops tmr ok")
                }
                if (objLoadSettings.m_MediumOpsInterval > 0)
                {
                    if ((ret = ev_tmr_add(tmr_load_settings_highops, ev_load_settings_timer_highops, objLoadSettings.m_HighOpsInterval * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                        LOG(*m_log, m_name, logger::Info, "starting high ops tmr failed " << ret)
                    else
                        LOG(*m_log, m_name, logger::Info, "starting high ops tmr ok")
                }
            }
            m_counter = 0;
            if ((ret = ev_tmr_add(tmr_load, ev_load_tmr, m_TmrIntervalMsecs * 1000, ev_epoll::ev_tmr_type_oneshot)) != E_ev_no_error)
                LOG(*m_log, m_name, logger::Info, "starting load tmr failed")
            else
                LOG(*m_log, m_name, logger::Info, "starting load tmr ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_new_order_bulk(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];
    int no_loops = 1;

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("B { \"Loops\" : 5, \"UserID\" : 1, \"UserTag\" : 1234, \"ExType\" : 1, \"OrdTrig\" : 1, \"InstrumentID\" : 18, \"OrderType\" : 2, \"Side\" : 1, \"Quantity\" : 1, \"DisclosedQuantity\" : 1, \"Price\" : 7689, \"TriggerPrice\" : 7869, \"TIF\" : 2, \"MarketPriceProtectionTicks\" : 0 } \n>> ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(NEW_ORDER_REQUEST);
            f.msg.nos_req.MessageType = mt1_new_order_request;
            if (doc.HasMember("Loops"))
                no_loops = doc["Loops"].GetInt();
            if (doc.HasMember("UserID"))
                f.msg.nos_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.nos_req.UserTag = doc["UserTag"].GetInt();
            // if (doc.HasMember("AccountType"))
            //     f.msg.nos_req.AccountType = doc["AccountType"].GetInt();
            // if (doc.HasMember("AccountID1"))
            //     {
            //     memset(f.msg.nos_req.AccountID1, 0, sizeof(f.msg.nos_req.AccountID1));
            //     strcpy((char *)f.msg.nos_req.AccountID1, doc["AccountID1"].GetString());
            //     }
            // if (doc.HasMember("AccountID2"))
            //     {
            //     memset(f.msg.nos_req.AccountID2, 0, sizeof(f.msg.nos_req.AccountID2));
            //     strcpy((char *)f.msg.nos_req.AccountID1, doc["AccountID2"].GetString());
            //     }
            if (doc.HasMember("InstrumentID"))
                f.msg.nos_req.OrderInstrumentID = doc["InstrumentID"].GetInt();
            if (doc.HasMember("OrderType"))
                f.msg.nos_req.OrderType = doc["OrderType"].GetInt();
            if (doc.HasMember("Side"))
                f.msg.nos_req.OrderSide = doc["Side"].GetInt();
            if (doc.HasMember("Quantity"))
                f.msg.nos_req.OrderQuantity = doc["Quantity"].GetInt();
            if (doc.HasMember("DisclosedQuantity"))
                f.msg.nos_req.OrderDisclosedQuantity = doc["DisclosedQuantity"].GetInt();
            if (doc.HasMember("Price"))
                f.msg.nos_req.OrderPrice = doc["Price"].GetInt();
            if (doc.HasMember("TriggerPrice"))
                f.msg.nos_req.OrderTriggerPrice = doc["TriggerPrice"].GetInt();
            if (doc.HasMember("TIF"))
                f.msg.nos_req.OrderTimeInForce = doc["TIF"].GetInt();
            if (doc.HasMember("ExType"))
                f.msg.nos_req.OrderExecutionType = doc["ExType"].GetInt();
            if (doc.HasMember("OrdTrig"))
                f.msg.nos_req.TriggerOn = doc["OrdTrig"].GetInt();
            if (doc.HasMember("MarketPriceProtectionTicks"))
                f.msg.nos_req.MarketPriceProtectionTicks = doc["MarketPriceProtectionTicks"].GetInt();
            std::thread(&binary_gateway_load_test_client::write_bulk_new_orders, this, f, no_loops).detach();
        }
    }
    return (E_ev_no_error);
}

void binary_gateway_load_test_client::write_bulk_new_orders(BINARY_FRAME f, int no_loops)
{
    LOG(*m_log, m_name, logger::Info, "start bulk write new orders")
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(1, &mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
        printf("cpu affinity failed\n");

    for (int i = 0; i < no_loops; ++i)
    {
        //        f.msg.nos_req.UserTag = timestamp::rdtsc64_nosync();
        f.msg.nos_req.UserTag = timestamp::epoch_usecs();
        if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(NEW_ORDER_REQUEST)) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed")
        else
        {
            //            LOG(*m_log, m_name, logger::Info, "tx new order single ok UserTag " << f.msg.nos_req.UserTag)
            //            usleep(1);
            timestamp::rdtsc_ticks_delay(50000);
        }
    }
    LOG(*m_log, m_name, logger::Info, "finish bulk write new orders")
    return;
}

int binary_gateway_load_test_client::process_a_modify_order(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("M { \"UserID\" : 1, \"UserTag\" : 1234, \"ExchangeOrderID\" : 1, \"InFlightMitigation\" : 1, \"OrderType\" : 1,\"QuantityRemaining\" : 1,\"ExType\" : 1, \"Quantity\" : 1000, \"DisclosedQuantity\" : 1000, \"Price\" : 7689, \"TriggerPrice\" : 7869, \"TIF\" : 2, \"MarketPriceProtectionTicks\" : 0  }\n>> ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(MODIFY_ORDER_REQUEST);
            f.msg.mo_req.MessageType = mt1_modify_order_request;
            if (doc.HasMember("UserID"))
                f.msg.mo_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.mo_req.UserTag = doc["UserTag"].GetInt();
            if (doc.HasMember("ExchangeOrderID"))
                f.msg.mo_req.ExchangeOrderID = doc["ExchangeOrderID"].GetInt();
            // if (doc.HasMember("InFlightMitigation"))
            //     f.msg.mo_req.InFlightMitigation = doc["InFlightMitigation"].GetInt();
            if (doc.HasMember("OrderType"))
                f.msg.mo_req.OrderType = doc["OrderType"].GetInt();
            if (doc.HasMember("Quantity"))
                f.msg.mo_req.OrderQuantity = doc["Quantity"].GetInt();
            if (doc.HasMember("DisclosedQuantity"))
                f.msg.mo_req.OrderDisclosedQuantity = doc["DisclosedQuantity"].GetInt();
            if (doc.HasMember("Price"))
                f.msg.mo_req.OrderPrice = doc["Price"].GetInt();
            if (doc.HasMember("TriggerPrice"))
                f.msg.mo_req.OrderTriggerPrice = doc["TriggerPrice"].GetInt();
            if (doc.HasMember("TIF"))
                f.msg.mo_req.OrderTimeInForce = doc["TIF"].GetInt();
            if (doc.HasMember("ExType"))
                f.msg.mo_req.OrderExecutionType = doc["ExType"].GetInt();
            if (doc.HasMember("QuantityRemaining"))
                f.msg.mo_req.OrderQuantityRemaining = doc["QuantityRemaining"].GetInt();

            if (doc.HasMember("MarketPriceProtectionTicks"))
                f.msg.mo_req.MarketPriceProtectionTicks = doc["MarketPriceProtectionTicks"].GetInt();
            if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(MODIFY_ORDER_REQUEST)) != 0)
                LOG(*m_log, m_name, logger::Info, "tx modify order request failed")
            else
                LOG(*m_log, m_name, logger::Info, "tx modify order request ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_cancel_order(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("C { \"UserID\" : 1234, \"UserTag\" : 1234, \"ExchangeOrderID\" : 12 }\n>> ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(CANCEL_ORDER_REQUEST);
            f.msg.co_req.MessageType = mt1_cancel_order_request;
            if (doc.HasMember("UserID"))
                f.msg.co_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.co_req.UserTag = doc["UserTag"].GetInt64();
            if (doc.HasMember("ExchangeOrderID"))
                f.msg.co_req.ExchangeOrderID = doc["ExchangeOrderID"].GetInt();
            if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(CANCEL_ORDER_REQUEST)) != 0)
                LOG(*m_log, m_name, logger::Info, "tx cancel order request failed")
            else
                LOG(*m_log, m_name, logger::Info, "tx cancel order request ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_cancel_all_orders(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("A { \"UserID\" : 1, \"UserTag\" : 1234, \"CancelGTC\" : 0 } ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(CANCEL_ALL_ORDERS_REQUEST);
            f.msg.cao_req.MessageType = mt1_cancel_all_orders_request;
            if (doc.HasMember("UserID"))
                f.msg.cao_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.cao_req.UserTag = doc["UserTag"].GetInt();
            if (doc.HasMember("CancelGTC"))
                f.msg.cao_req.cancel_gtc = doc["CancelGTC"].GetInt();
            if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(CANCEL_ALL_ORDERS_REQUEST)) != 0)
                LOG(*m_log, m_name, logger::Info, "tx cancel all orders request failed")
            else
                LOG(*m_log, m_name, logger::Info, "tx cancel all orders request ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_a_cancel_replace(char *buf)
{
    // BINARY_FRAME f;
    // char tmp[512];

    // while (*buf && *buf != '{')
    //     ++buf;
    // if (*buf == 0)
    //     {
    //     printf("R { \"UserID\" : 12, \"UserTag\" : 1234, \"ExchangeOrderID\" : 12, \"AccountType\" : 12, \"AccountID\" : 1234, \"InstrumentID\" : 123456, \"OrderType\" : 0, \"Side\" : 2, \"Quantity\" : 1000, \"DisclosedQuantity\" : 1000, \"Price\" : 7689, \"TriggerPrice\" : 7869, \"TIF\" : 2 }\n>> ");
    //     fflush(stdout);
    //     }
    // else
    //     {
    //     Document doc;

    //     strcpy(tmp, buf);
    //     if (doc.ParseInsitu(tmp).HasParseError())
    //         LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
    //     else
    //         {
    //         f.hdr.msg_len = sizeof(CANCEL_REPLACE_ORDER_REQUEST);
    //         f.msg.cro_req.MessageType = mt1_cancel_replace_order_request;
    //         if (doc.HasMember("UserID"))
    //             f.msg.cro_req.UserID = doc["UserID"].GetInt();
    //         if (doc.HasMember("UserTag"))
    //             f.msg.cro_req.UserTag = doc["UserTag"].GetInt();
    //         if (doc.HasMember("ExchangeOrderID"))
    //             f.msg.cro_req.ExchangeOrderID = doc["ExchangeOrderID"].GetInt();
    //         if (doc.HasMember("AccountType"))
    //             f.msg.cro_req.AccountType = doc["AccountType"].GetInt();
    //         if (doc.HasMember("AccountID1"))
    //             {
    //             memset(f.msg.cro_req.AccountID1, 0, sizeof(f.msg.cro_req.AccountID1));
    //             strcpy((char *)f.msg.cro_req.AccountID1, doc["AccountID1"].GetString());
    //             }
    //         if (doc.HasMember("AccountID2"))
    //             {
    //             memset(f.msg.cro_req.AccountID2, 0, sizeof(f.msg.cro_req.AccountID2));
    //             strcpy((char *)f.msg.cro_req.AccountID2, doc["AccountID2"].GetString());
    //             }
    //         if (doc.HasMember("InstrumentID"))
    //             f.msg.cro_req.OrderInstrumentID = doc["InstrumentID"].GetInt();
    //         if (doc.HasMember("OrderType"))
    //             f.msg.cro_req.OrderType = doc["OrderType"].GetInt();
    //         if (doc.HasMember("Side"))
    //             f.msg.cro_req.OrderSide = doc["Side"].GetInt();
    //         if (doc.HasMember("Quantity"))
    //             f.msg.cro_req.OrderQuantity = doc["Quantity"].GetInt();
    //         if (doc.HasMember("DisclosedQuantity"))
    //             f.msg.cro_req.OrderDisclosedQuantity = doc["DisclosedQuantity"].GetInt();
    //         if (doc.HasMember("Price"))
    //             f.msg.cro_req.OrderPrice = doc["Price"].GetInt();
    //         if (doc.HasMember("TriggerPrice"))
    //             f.msg.cro_req.OrderTriggerPrice = doc["TriggerPrice"].GetInt();
    //         if (doc.HasMember("TIF"))
    //             f.msg.cro_req.OrderTimeInForce = doc["TIF"].GetInt();
    //         if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(CANCEL_REPLACE_ORDER_REQUEST)) != 0)
    //             LOG(*m_log, m_name, logger::Info, "tx cancel replace order request failed")
    //         else
    //             LOG(*m_log, m_name, logger::Info, "tx cancel replace order request ok")
    //         }
    //     }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_working_orders(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("W { \"UserID\" : 1234, \"UserTag\" : 1234 } ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(WORKING_ORDERS_REQUEST);
            f.msg.wo_req.MessageType = mt1_working_orders_request;
            if (doc.HasMember("UserID"))
                f.msg.wo_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.wo_req.UserTag = doc["UserTag"].GetInt();
            if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(WORKING_ORDERS_REQUEST)) != 0)
                LOG(*m_log, m_name, logger::Info, "tx working orders request failed")
            else
                LOG(*m_log, m_name, logger::Info, "tx working orders request ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_get_fills(char *buf)
{
    BINARY_FRAME f;
    char tmp[512];

    while (*buf && *buf != '{')
        ++buf;
    if (*buf == 0)
    {
        printf("F { \"UserID\" : 1234, \"UserTag\" : 1234 } ");
        fflush(stdout);
    }
    else
    {
        Document doc;

        strcpy(tmp, buf);
        if (doc.ParseInsitu(tmp).HasParseError())
            LOG(*m_log, m_name, logger::Info, "parse json doc failed (" << buf << ")")
        else
        {
            f.hdr.msg_len = sizeof(FILL_REQUEST);
            f.msg.fill_req.MessageType = mt1_fill_request;
            if (doc.HasMember("UserID"))
                f.msg.fill_req.UserID = doc["UserID"].GetInt();
            if (doc.HasMember("UserTag"))
                f.msg.fill_req.UserTag = doc["UserTag"].GetInt();
            if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(FILL_REQUEST)) != 0)
                LOG(*m_log, m_name, logger::Info, "tx fills request failed")
            else
                LOG(*m_log, m_name, logger::Info, "tx fills request ok")
        }
    }
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_new_order_single_response(NEW_ORDER_RESPONSE &m)
{
    long st = timestamp::epoch_usecs();

    if (m_user_tags.find(m.UserTag) == m_user_tags.end())
    {
        LOG(*m_log, m_name, logger::Error, "rx new order response cant find tag(" << m.UserTag << ") in set")
    }

    else if (m_user_tags.erase(m.UserTag) != 1)
        LOG(*m_log, m_name, logger::Error, "rx new order response erase tag didnt return 1")
    LOG(*m_log, m_name, logger::Info, "rx new order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")     Diff usecs(" << (st - m.UserTag) << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_modify_order_response(MODIFY_ORDER_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx modify order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_cancel_order_response(CANCEL_ORDER_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_cancel_all_orders_response(CANCEL_ALL_ORDERS_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel all orders response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") TotalOrdersCanceled(" << m.TotalOrdersCanceled << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

// int binary_gateway_load_test_client::process_cancel_replace_order_response(CANCEL_REPLACE_ORDER_RESPONSE &m)
//     {
//     LOG(*m_log, m_name, logger::Info, "rx cancel replace order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") New ExchangeOrderID(" << m.ExchangeOrderID <<
//         ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
//     return (E_ev_no_error);
//     }

int binary_gateway_load_test_client::process_working_orders_response(WORKING_ORDERS_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx working orders response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") NumberOfWorkingOrders(" << m.NumberOfWorkingOrders << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_fill_response(FILL_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx number fill response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") NumberOfFills(" << m.NumberOfFills << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_working_order_notification(WORKING_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx working order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") OrderQuantityRemaining(" << m.OrderQuantityRemaining << ") OrderInstrumentID(" << m.OrderInstrumentID << ") OrderType(" << (int)m.OrderType << ") OrderSide(" << (int)m.OrderSide << ") OrderQuantity(" << m.OrderQuantity << ") OrderDisclosedQuantity(" << m.OrderDisclosedQuantity << ") OrderPrice(" << m.OrderPrice << ") OrderTriggerPrice(" << m.OrderTriggerPrice << ") OrderTimeInForce(" << (int)m.OrderTimeInForce << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_modify_order_notification(MODIFY_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx modify order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") OrderType(" << (int)m.OrderType << ") OrderQuantity(" << m.OrderQuantity << ") OrderDisclosedQuantity(" << m.OrderDisclosedQuantity << ") OrderPrice(" << m.OrderPrice << ") OrderTriggerPrice(" << m.OrderTriggerPrice << ") OrderTimeInForce(" << (int)m.OrderTimeInForce << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_cancel_order_notification(CANCEL_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_fill_notification(FILL_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx fill notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") InstrumentID(" << m.InstrumentID << ") OrderSide(" << (int)m.OrderSide << ") OrderQuantityRemaining(" << m.OrderQuantityRemaining << ") FillQuantity(" << m.FillQuantity << ") FillPrice(" << m.FillPrice << ") FillID(" << m.FillID << ") TradeID(" << m.TradeID << ") MatchID(" << m.MatchID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int binary_gateway_load_test_client::process_session_notification(SESSION_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx session notification status(" << m.StatusID << ") Timestamp (" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

// int binary_gateway_load_test_client::process_stop_trigger_notification(STOP_TRIGGER_NOTIFICATION &m)
//     {
//     LOG(*m_log, m_name, logger::Info, "rx stop trigger notification UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID <<
//         ") InstrumentID(" << m.InstrumentID << ") OrderTriggerPrice(" << m.OrderTriggerPrice << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
//     return (E_ev_no_error);
//     }

int binary_gateway_load_test_client::process_gateway_client_logoff_notification(GATEWAY_CLIENT_LOGOFF_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx logoff notification StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

void my_thread(binary_gateway_load_test_client *p)
{
    p->run();
}

static const char *examples[] = {
    "db", "hello", "hallo", "hans", "hansekogge", "seamann", "quetzalcoatl", "quit", "q", "power", NULL};

void completionHook(char const *prefix, linenoiseCompletions *lc)
{
    size_t i;

    for (i = 0; examples[i] != NULL; ++i)
    {
        if (strncmp(prefix, examples[i], strlen(prefix)) == 0)
        {
            linenoiseAddCompletion(lc, examples[i]);
        }
    }
}

int main(int ac, char **av)
{
    char *line;
    bool running = true;

    if (ac != 6)
    {
        printf("\n\n\nUsage:    binary_gateway_load_test_client <Logon ID> <Password> <IP> <Port> <root filename for latency/log files>\n\n\n");
        exit(0);
    }
    x.set_display_range(10);
    binary_gateway_load_test_client client("MAIN", av[1], av[2], av[3], av[4], av[5]);
    std::thread xxx(my_thread, &client);
    while (running == true)
    {
        line = linenoise(">> ");
        if (line != NULL)
        {
            linenoiseHistoryAdd(line);
            running = strcmp(line, "q");
            client.send_input(line);
            free(line);
        }
    }
    xxx.join();
    return (0);
}
