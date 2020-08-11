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
#include <../api/moonx_binary_api.hpp>
#include "sample_test_client_moonx.hpp"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <restclient-cpp/restclient.h>

using namespace std;
using namespace rapidjson;

const char *bool_str(bool value)
{
    return (value ? "Yes" : "No");
}

latency x;

sample_test_client_moonx::sample_test_client_moonx(const std::string &name, const std::string &gateway_client_id, const std::string password, const std::string omgip, const std::string omgport, const std::string mdgip, const std::string mdgport, const std::string root_filename) : generic_framer1(2, 1024), ev_manager(name.c_str(), 1), m_tcpc(1024 * 1024 * 6, 1024 * 1024 * 6),m_mdg_tcpc(1024 * 1024 * 6, 1024 * 1024 * 6)
{
    m_gateway_client_id = gateway_client_id;
    m_name = name;
    m_password = password;
    m_ip = omgip;
    m_port = omgport;

    m_mdg_ip = mdgip;
    m_mdg_port = mdgport;

    m_log_filename = root_filename + ".log";
    m_latency_filename = root_filename + ".latency";
    m_main_task_id = -1;
    m_state = task_initialise;
    m_log = new logger(cout, m_log_filename.c_str());
    m_log->set_level(logger::Info);
    m_log->disable_buffered();
   // m_log->enable_buffered();
  //  m_log->enable_async();
    m_log->enable_file();
   // m_log->disable_stream();
    m_pfunc[task_initialise] = &sample_test_client_moonx::fsm_initialise;
    m_event_delete = true;
    m_con = 0;
    m_tcp_rx_idle_count = 0;
    m_tcp_tx_idle_count = 0;
    m_tcp_connected = false;
    m_idle_count = 0;
    m_config_file = "config.txt";
    return;
}

sample_test_client_moonx::~sample_test_client_moonx()
{
    m_log->disable_buffered();
    m_log->flush();
    delete m_log;
    return;
}

int sample_test_client_moonx::run()
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

int sample_test_client_moonx::ev_system_event_callback(int event)
{
    ev_event sys_event(event);

    return ((this->*m_pfunc[m_state])(&sys_event));
}

int sample_test_client_moonx::ev_app_event_callback(ev_event *pev)
{
    int ret, ret1 = E_ev_no_error;

    m_event_delete = true;
    ret = (this->*m_pfunc[m_state])(pev);
    if (m_event_delete == true)
        ret1 = ev_free(pev);
    return ((ret == E_ev_no_error) ? ret1 : ret);
}

int sample_test_client_moonx::fsm_initialise(ev_event *pev)
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

    case ev_tcp_read:
        ret = tcp_read();
        break;
    case ev_tcp_read_mdg:
        ret = tcp_read_mdg();
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

int sample_test_client_moonx::write_latency_file()
{
    LOG(*m_log, m_name, logger::Info, "start writing latency file")
    m_latency.set_display_range(5);
    m_latency.calc(m_latency_filename, true);
    LOG(*m_log, m_name, logger::Info, "finish writing latency file")
    return (E_ev_no_error);
}

int sample_test_client_moonx::initialise_task()
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
   

    LOG(*m_log, m_name, logger::Info, "connecting to OMG " )
    ret = m_tcpc.set_remote_host(m_ip);
    ret = m_tcpc.set_remote_port(m_port);
    ret = m_tcpc.connect(&m_con);
    if (ret != E_tcp_no_error)
        LOG(*m_log, m_name, logger::Info, "tcp(omg) connect failed " << ret)
    else
    {
        LOG(*m_log, m_name, logger::Info, "tcp(omg) connect ok IP: " << m_ip << " Port: " << m_port)
        if ((ret = ev_fd_add(m_con->get_fd(), ev_epoll::ev_fd_type_read, ev_tcp_read)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp(omg) failed to add read fd on active connection " << ret)
        else if ((ret = ev_fd_add(m_con->get_fd(), ev_epoll::ev_fd_type_except, ev_tcp_exception)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp(omg) failed to add exception fd on active connection " << ret)
        else
        {
            LOG(*m_log, m_name, logger::Info, "tcp(omg) connection active on fd " << m_con->get_fd())
            m_tcp_connected = true;
            process_a_logon();
        }
    }
    LOG(*m_log, m_name, logger::Info, "connecting to MDG " )
    ret = m_mdg_tcpc.set_remote_host(m_mdg_ip);
    ret = m_mdg_tcpc.set_remote_port(m_mdg_port);
    ret = m_mdg_tcpc.connect(&m_mdg_con);
    if (ret != E_tcp_no_error)
        LOG(*m_log, m_name, logger::Info, "tcp(mdg) connect  failed " << ret)
    else
    {
        LOG(*m_log, m_name, logger::Info, "tcp(mdg) connect ok IP: " << m_mdg_ip << " Port: " << m_mdg_port)
        if ((ret = ev_fd_add(m_mdg_con->get_fd(), ev_epoll::ev_fd_type_read, ev_tcp_read)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp(mdg) failed to add read fd on active connection " << ret)
        else if ((ret = ev_fd_add(m_mdg_con->get_fd(), ev_epoll::ev_fd_type_except, ev_tcp_exception)) != E_ev_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp(mdg) failed to add exception fd on active connection " << ret)
        else
        {
            LOG(*m_log, m_name, logger::Info, "tcp(mdg) connection active on fd " << m_con->get_fd())
            m_tcp_connected = true;
        }
    }
    if(m_tcp_connected)
    {
        send_input("?");
    }
    return (E_ev_no_error);
}



int sample_test_client_moonx::process_idle_tmr()
{
    //LOG(*m_log, m_name, logger::Info, "timer event recieved  ")
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

map<int, pair<int, int>> m_instruments;


int sample_test_client_moonx::check_set()
{
    LOG(*m_log, m_name, logger::Info, "check set, the set has " << m_user_tags.size() << " items in it")
    for (auto &p : m_user_tags)
        LOG(*m_log, m_name, logger::Info, "set tag " << p)
    m_user_tags.clear();
    return (E_ev_no_error);
}

int sample_test_client_moonx::tcp_read()
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
int sample_test_client_moonx::tcp_read_mdg()
{
    int ret;
    int len;
    char buf[32768];

    if ((ret = m_mdg_con->read_data(buf, sizeof(buf), len)) == E_tcp_no_error)
    {
        m_tcp_rx_idle_count = 0;
        if ((ret = parse_raw_rx_data(1, buf, len)) != E_frmr_no_error)
            LOG(*m_log, m_name, logger::Info, "tcp(mdg) process raw data has failed, error " << ret)
    }
    else if (ret == E_tcp_would_block)
        ret = E_tcp_no_error;
    else
        LOG(*m_log, m_name, logger::Info, "tcp read has failed, error " << ret)
    if (ret != E_tcp_no_error)
        ret = ev_terminate();
    return (ret);
}
bool g_cLogonResponse =false;
int sample_test_client_moonx::process_rx_frame(unsigned int index, const char *src, unsigned int len)
{
    
    int ret = E_ev_no_error;
    BINARY_INTERFACE_MESSAGES *m;

    if (len < 4)
        LOG(*m_log, m_name, logger::Info, "rx frame must be at least 4 characters long to form a proper message, dump" << hexdump(src, len, 32))
    else
    {
        LOG(*m_log, m_name, logger::Info, "stream index " << index)
        src += sizeof(FRAME_HDR);
        len -= sizeof(FRAME_HDR);
        m = (BINARY_INTERFACE_MESSAGES *)src;
        if(index ==1)
        {
            LOG(*m_log, m_name, logger::Info, "stream index " << index << " m->msg_type "<<m->msg_type)
            //check mdg messages
             switch (m->msg_type)
            {
            case md_mt_heartbeat:
                LOG(*m_log, m_name, logger::Info, "rx(mdg) heartbeat received " )
            break;
            case md_mt_trade_update:
                LOG(*m_log, m_name, logger::Info, "rx(mdg) trade update received " )
            break;
            case md_mt_instrument_state_update:
                 LOG(*m_log, m_name, logger::Info, "rx(mdg) instrument state update received " )
            break;
            case md_mt_depth_10_update:
                LOG(*m_log, m_name, logger::Info, "rx(mdg) market depth received " )
            break;
            
            default:
             LOG(*m_log, m_name, logger::Info, "rx(mdg) unknown message " )
            break;
            }
            return 0;
        }

        switch (m->msg_type)
        {
        case mt1_server_heartbeat:
            //LOG(*m_log, m_name, logger::Info, "rx heartbeat ok")
            break;
        case mt1_gateway_client_logon_response:
            
            LOG(*m_log, m_name, logger::Info, "rx gateway client logon response ok StatusID(" << m->gclg_res.StatusID << ") Timestamp(" << m->gclg_res.TimeStamp << ")")
            if( m->gclg_res.StatusID == 0 )
            {
                g_cLogonResponse =true;
            } 
            break;
        case mt1_gateway_client_logoff_response:
            LOG(*m_log, m_name, logger::Info, "rx gateway client logoff response ok status " << m->gclo_res.StatusID)
            break;
        case mt1_new_order_response:

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
    if(ret == 0)
      m_idle_count =0;
    return (ret);
}

int sample_test_client_moonx::tcp_write(char *src, int len)
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

int sample_test_client_moonx::tcp_exception()
{
    LOG(*m_log, m_name, logger::Info, "tcp exception on tcp client listen connection")
    return (0);
}

int sample_test_client_moonx::p_set_flags(int fd, int flags)
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

int sample_test_client_moonx::console_read(ev_event *pev)
{
    console_input *i = (console_input *)pev;
    int ret = E_ev_no_error;

    LOG(*m_log, m_name, logger::Info, "console command (" << i->buf << ")")
    if (!strncasecmp(i->buf, "Q", 1))
    {
        LOG(*m_log, m_name, logger::Info, "test harness requested to terminate")
        ret = ev_terminate();
    }
    else if (!strncasecmp(i->buf, "12", 1))
    {
        ret = process_a_logoff(i->buf);
    }
    else if (!strncasecmp(i->buf, "1", 1))
    {
       // NEW_ORDER_REQUEST objNewOrder;
        ret = prompt_new_order();
        
    }
    else if (!strncasecmp(i->buf, "2", 1))
    {
        // MODIFY_ORDER_REQUEST objModifyOrder;
        ret = prompt_modify_order();
    }
    else if (!strncasecmp(i->buf, "3", 1))
    {
        ret = prompt_cancel_order();
    }
    else if (!strncasecmp(i->buf, "4", 1))
    {
        ret = prompt_cancel_all_orders();
    }
    else if (!strncasecmp(i->buf, "5", 1))
    {
        ret = prompt_modify_leverage();
    }
    else if (!strncasecmp(i->buf, "6", 1))
    {
        ret = prompt_working_orders_request();
    }
    else if (!strncasecmp(i->buf, "7", 1))
    {
        ret = prompt_fills_request();
    }
    else if (!strncasecmp(i->buf, "8", 1))
    {
        ret = prompt_asset_positions();
    }
    else if (!strncasecmp(i->buf, "9", 1))
    {
        ret = prompt_symbols();
    }
    else if (!strncasecmp(i->buf, "10", 1))
    {
        ret = prompt_depth();
    }
    else if (!strncasecmp(i->buf, "11", 1))
    {

        ret = process_a_logoff(i->buf);
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

int sample_test_client_moonx::print_menu()
{

    printf("Cmnds\n");
    printf("1.  create New Order\n");
    printf("2.  modify an order\n");
    printf("3.  cancel an order\n");
    printf("4.  cancel all order\n");
    printf("5.  modify leverage\n");
    printf("6.  get working order\n");
    printf("7.  get fills\n");
    printf("8.  asset query\n");
    printf("9.  symbols\n");
    printf("10. depth\n");
   // printf("11. asset and position\n");
    printf("11. Log off\n");
    printf("?   this menu\n");
    fflush(stdout);
    return (E_ev_no_error);
}

int sample_test_client_moonx::do_latency()
{
    x.calc("client_latency.txt", true);
    return (E_ev_no_error);
}

int sample_test_client_moonx::send_heartbeat()
{
    // if(!g_cLogonResponse)
    // return E_ev_no_error;
    BINARY_FRAME f;

    f.hdr.msg_len = sizeof(HEARTBEAT);
    f.msg.nos_req.MessageType = mt1_client_heartbeat;
    if (tcp_write((char *)&f, sizeof(f.hdr) + sizeof(HEARTBEAT)) != 0)
        LOG(*m_log, m_name, logger::Info, "tx hearbeat failed")
    else{}
         // LOG(*m_log, m_name, logger::Info, "tx heartbeat ok")
        return (E_ev_no_error);
}

int sample_test_client_moonx::process_a_logon()
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

int sample_test_client_moonx::process_a_logoff(char *buf)
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


int sample_test_client_moonx::process_a_modify_order(char *buf)
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

int sample_test_client_moonx::process_a_cancel_order(char *buf)
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

int sample_test_client_moonx::process_a_cancel_all_orders(char *buf)
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

int sample_test_client_moonx::process_a_cancel_replace(char *buf)
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

int sample_test_client_moonx::process_working_orders(char *buf)
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

int sample_test_client_moonx::process_get_fills(char *buf)
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

int sample_test_client_moonx::process_new_order_single_response(NEW_ORDER_RESPONSE &m)
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

int sample_test_client_moonx::process_modify_order_response(MODIFY_ORDER_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx modify order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_cancel_order_response(CANCEL_ORDER_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel order response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_cancel_all_orders_response(CANCEL_ALL_ORDERS_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel all orders response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") TotalOrdersCanceled(" << m.TotalOrdersCanceled << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}



int sample_test_client_moonx::process_working_orders_response(WORKING_ORDERS_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx working orders response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") NumberOfWorkingOrders(" << m.NumberOfWorkingOrders << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_fill_response(FILL_RESPONSE &m)
{
    LOG(*m_log, m_name, logger::Info, "rx number fill response UserID(" << m.UserID << ") UserTag(" << m.UserTag << ") NumberOfFills(" << m.NumberOfFills << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_working_order_notification(WORKING_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx working order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") OrderQuantityRemaining(" << m.OrderQuantityRemaining << ") OrderInstrumentID(" << m.OrderInstrumentID << ") OrderType(" << (int)m.OrderType << ") OrderSide(" << (int)m.OrderSide << ") OrderQuantity(" << m.OrderQuantity << ") OrderDisclosedQuantity(" << m.OrderDisclosedQuantity << ") OrderPrice(" << m.OrderPrice << ") OrderTriggerPrice(" << m.OrderTriggerPrice << ") OrderTimeInForce(" << (int)m.OrderTimeInForce << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_modify_order_notification(MODIFY_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx modify order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") OrderType(" << (int)m.OrderType << ") OrderQuantity(" << m.OrderQuantity << ") OrderDisclosedQuantity(" << m.OrderDisclosedQuantity << ") OrderPrice(" << m.OrderPrice << ") OrderTriggerPrice(" << m.OrderTriggerPrice << ") OrderTimeInForce(" << (int)m.OrderTimeInForce << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_cancel_order_notification(CANCEL_ORDER_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx cancel order notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_fill_notification(FILL_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx fill notification UserID(" << m.UserID << ") ExchangeOrderID(" << m.ExchangeOrderID << ") InstrumentID(" << m.InstrumentID << ") OrderSide(" << (int)m.OrderSide << ") OrderQuantityRemaining(" << m.OrderQuantityRemaining << ") FillQuantity(" << m.FillQuantity << ") FillPrice(" << m.FillPrice << ") FillID(" << m.FillID << ") TradeID(" << m.TradeID << ") MatchID(" << m.MatchID << ") ExchangeEventID(" << m.ExchangeEventID << ") StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

int sample_test_client_moonx::process_session_notification(SESSION_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx session notification status(" << m.StatusID << ") Timestamp (" << m.TimeStamp << ")")
    return (E_ev_no_error);
}



int sample_test_client_moonx::process_gateway_client_logoff_notification(GATEWAY_CLIENT_LOGOFF_NOTIFICATION &m)
{
    LOG(*m_log, m_name, logger::Info, "rx logoff notification StatusID(" << m.StatusID << ") Timestamp(" << m.TimeStamp << ")")
    return (E_ev_no_error);
}

void my_thread(sample_test_client_moonx *p)
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

    if (ac != 8)
    {
        printf("\n\n\nUsage:    sample_test_client_moonx <Logon ID> <Password> <OMS IP> <OMS Port> <MDG IP> <MDG Port> <root filename for latency/log files>\n\n\n");
        exit(0);
    }

    sample_test_client_moonx client("MAIN", av[1], av[2], av[3], av[4],av[5],av[6], av[7]);
   

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

int sample_test_client_moonx::prompt_new_order()
{
    NEW_ORDER_REQUEST orderobject={0};
        
    static NEW_ORDER_REQUEST lastorderobject; 

    string strInput;

    prompt("UserId id ",orderobject.UserID,lastorderobject.UserID);
    prompt("User Tag ",orderobject.UserID,lastorderobject.UserTag);
    prompt("Order Instrument id ",orderobject.OrderInstrumentID,lastorderobject.OrderInstrumentID);
    prompt("Order Time In Force ",orderobject.OrderTimeInForce,lastorderobject.OrderType);

    prompt("Order Side ",orderobject.OrderSide,lastorderobject.OrderSide);
    prompt("Order Execution Type ",orderobject.OrderExecutionType,lastorderobject.OrderExecutionType);

    prompt("Order Quantity ",orderobject.OrderQuantity,lastorderobject.OrderQuantity);
    prompt("Order Disclosed Quantity ",orderobject.OrderDisclosedQuantity,lastorderobject.OrderDisclosedQuantity);

    prompt("Order Price ",orderobject.OrderPrice,lastorderobject.OrderPrice);

    prompt("Order Trigger Price ",orderobject.OrderTriggerPrice,lastorderobject.OrderTriggerPrice);
    prompt("Trigger On ",orderobject.TriggerOn,lastorderobject.TriggerOn);


    lastorderobject = orderobject;
    process_new_order(orderobject);

    return 0;
}
int sample_test_client_moonx::process_new_order(NEW_ORDER_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.nos_req = orderobject;

    f.hdr.msg_len = sizeof(NEW_ORDER_REQUEST);
    f.msg.nos_req.MessageType = mt1_new_order_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " <<  f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " <<f.msg.nos_req.MessageType                                       << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.nos_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.nos_req.UserTag                    << ", "
     << '"' << "InstrumentID"                   << '"' << " : " << f.msg.nos_req.OrderInstrumentID          << ", " 
     << '"' << "OrderType"                      << '"' << " : " << (int)f.msg.nos_req.OrderType             << ", " 
     << '"' << "TIF"                            << '"' << " : " << (int)f.msg.nos_req.OrderTimeInForce      << ", " 
     << '"' << "Side"                           << '"' << " : " << (int)f.msg.nos_req.OrderSide             << ", " 
     << '"' << "ExType"                         << '"' << " : " << (int)f.msg.nos_req.OrderExecutionType    << ", "
     << '"' << "Quantity"                       << '"' << " : " << f.msg.nos_req.OrderQuantity              << ", " 
     << '"' << "DisclosedQuantity"              << '"' << " : " << f.msg.nos_req.OrderDisclosedQuantity     << ", " 
     << '"' << "Price"                          << '"' << " : " << f.msg.nos_req.OrderPrice                 << ", "
     << '"' << "MarketPriceProtectionTicks"     << '"' << " : " << f.msg.nos_req.MarketPriceProtectionTicks << ", " 
     << '"' << "TriggerPrice"                   << '"' << " : " << f.msg.nos_req.OrderTriggerPrice          << ", " 
     << '"' << "OrdTrigOn"                      << '"' << " : " << (int)f.msg.nos_req.TriggerOn << " }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(NEW_ORDER_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
     

    return ret;
}
int sample_test_client_moonx::prompt_modify_order()
    {

MODIFY_ORDER_REQUEST orderobject={0};
    static MODIFY_ORDER_REQUEST lastorderobject; 

    string strInput;

    prompt("UserId id ",orderobject.UserID,lastorderobject.UserID);
    prompt("User Tag ",orderobject.UserID,lastorderobject.UserTag);
    prompt("Exchange Order ID ",orderobject.ExchangeOrderID,lastorderobject.ExchangeOrderID);
    prompt("OrderQuantityRemaining ",orderobject.OrderQuantityRemaining,lastorderobject.OrderQuantityRemaining);

    prompt("Order Type ",orderobject.OrderType,lastorderobject.OrderType);
    prompt("OrderTimeInForce ",orderobject.OrderTimeInForce,lastorderobject.OrderTimeInForce);
    prompt("Order Execution Type ",orderobject.OrderExecutionType,lastorderobject.OrderExecutionType);

    prompt("Order Quantity ",orderobject.OrderQuantity,lastorderobject.OrderQuantity);
    prompt("Order Disclosed Quantity ",orderobject.OrderDisclosedQuantity,lastorderobject.OrderDisclosedQuantity);

    prompt("Order Price ",orderobject.OrderPrice,lastorderobject.OrderPrice);

    prompt("Order Trigger Price ",orderobject.OrderTriggerPrice,lastorderobject.OrderTriggerPrice);
    prompt("Trigger On ",orderobject.TriggerOn,lastorderobject.TriggerOn);


    lastorderobject = orderobject;
    return process_modify_order(orderobject);

    }

int sample_test_client_moonx::process_modify_order(MODIFY_ORDER_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.mo_req = orderobject;

    f.hdr.msg_len = sizeof(MODIFY_ORDER_REQUEST);
    f.msg.mo_req.MessageType = mt1_modify_order_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " << f.msg.mo_req.MessageType                                      << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.mo_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.mo_req.UserTag                    << ", "
     << '"' << "ExchangeOrderID"                   << '"' << " : " << f.msg.mo_req.ExchangeOrderID          << ", " 
     << '"' << "OrderQuantityRemaining"                   << '"' << " : " << f.msg.mo_req.OrderQuantityRemaining          << ", " 
     << '"' << "OrderType"                      << '"' << " : " << (int)f.msg.mo_req.OrderType             << ", " 
     << '"' << "TIF"                            << '"' << " : " << (int)f.msg.mo_req.OrderTimeInForce      << ", " 
     //<< '"' << "Side"                           << '"' << " : " << (int)f.msg.mo_req.OrderSide             << ", " 
     << '"' << "ExType"                         << '"' << " : " << (int)f.msg.mo_req.OrderExecutionType    << ", "
     << '"' << "Quantity"                       << '"' << " : " << f.msg.mo_req.OrderQuantity              << ", " 
     << '"' << "DisclosedQuantity"              << '"' << " : " << f.msg.mo_req.OrderDisclosedQuantity     << ", " 
     << '"' << "Price"                          << '"' << " : " << f.msg.mo_req.OrderPrice                 << ", "
     << '"' << "MarketPriceProtectionTicks"     << '"' << " : " << f.msg.mo_req.MarketPriceProtectionTicks << ", " 
     << '"' << "TriggerPrice"                   << '"' << " : " << f.msg.mo_req.OrderTriggerPrice          << ", " 
     << '"' << "OrdTrigOn"                      << '"' << " : " << (int)f.msg.mo_req.TriggerOn << " }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(MODIFY_ORDER_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return ret;
}


int sample_test_client_moonx::prompt_cancel_order()
    {


CANCEL_ORDER_REQUEST orderobject={0};
    static CANCEL_ORDER_REQUEST lastorderobject; 

    string strInput;

    prompt("UserId id ",orderobject.UserID,lastorderobject.UserID);
    prompt("User Tag ",orderobject.UserID,lastorderobject.UserTag);
    prompt("Exchange Order ID ",orderobject.ExchangeOrderID,lastorderobject.ExchangeOrderID);
   

    lastorderobject = orderobject;
    return process_cancel_order(orderobject);

    }

int sample_test_client_moonx::process_cancel_order(CANCEL_ORDER_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.co_req = orderobject;

    f.hdr.msg_len = sizeof(CANCEL_ORDER_REQUEST);
    f.msg.co_req.MessageType = mt1_cancel_order_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " << f.msg.co_req.MessageType                                       << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.co_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.co_req.UserTag                    << ", "
     << '"' << "ExchangeOrderID"                   << '"' << " : " << f.msg.co_req.ExchangeOrderID          << ", "
      <<" }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(CANCEL_ORDER_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return 0;
}




int sample_test_client_moonx::prompt_cancel_all_orders()
    {



CANCEL_ALL_ORDERS_REQUEST orderobject={0};
    static CANCEL_ALL_ORDERS_REQUEST lastorderobject; 

    string strInput;

    prompt("UserId id ",orderobject.UserID,lastorderobject.UserID);
    prompt("User Tag ",orderobject.UserID,lastorderobject.UserTag);
   

    lastorderobject = orderobject;
    return process_cancel_all_orders(orderobject);

    }

int sample_test_client_moonx::process_cancel_all_orders(CANCEL_ALL_ORDERS_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.cao_req = orderobject;

    f.hdr.msg_len = sizeof(CANCEL_ALL_ORDERS_REQUEST);
    f.msg.cao_req.MessageType = mt1_cancel_all_orders_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " << f.msg.cao_req.MessageType                                      << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.cao_req.UserID                     << ", " 
  
      <<" }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(CANCEL_ALL_ORDERS_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx new order single failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return 0;
}


int sample_test_client_moonx::prompt_modify_leverage()
    {


MODIFY_LEVERAGE_REQUEST modifyLeverageRequest={0};
    static MODIFY_LEVERAGE_REQUEST lastmodifyLeverageRequest; 

    string strInput;

    prompt("UserId id ",modifyLeverageRequest.UserID,lastmodifyLeverageRequest.UserID);
    prompt("User Tag ",modifyLeverageRequest.UserID,lastmodifyLeverageRequest.UserTag);
    prompt("Instrument ID ",modifyLeverageRequest.InstrumentID,lastmodifyLeverageRequest.InstrumentID);
    prompt("LeverageLeverageLeverage ",modifyLeverageRequest.Leverage,lastmodifyLeverageRequest.Leverage);
   

    lastmodifyLeverageRequest = modifyLeverageRequest;
    return process_modify_leverage(modifyLeverageRequest);

    }

int sample_test_client_moonx::process_modify_leverage(MODIFY_LEVERAGE_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.mlev_req = orderobject;

    f.hdr.msg_len = sizeof(MODIFY_LEVERAGE_REQUEST);
    f.msg.mlev_req.MessageType = mt1_modify_leverage_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " <<  f.msg.mlev_req.MessageType                                      << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.mlev_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.mlev_req.UserTag                    << ", "
     << '"' << "InstrumentID"                   << '"' << " : " << f.msg.mlev_req.InstrumentID          
     << " }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(MODIFY_LEVERAGE_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx modify leverage request failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return 0;
}


int sample_test_client_moonx::prompt_working_orders_request()
    {



WORKING_ORDERS_REQUEST workingOrdersRequest={0};
    static WORKING_ORDERS_REQUEST lastworkingOrdersRequest; 

    string strInput;

    prompt("UserId id ",workingOrdersRequest.UserID,lastworkingOrdersRequest.UserID);
    prompt("User Tag ",workingOrdersRequest.UserID,lastworkingOrdersRequest.UserTag);
    

    lastworkingOrdersRequest = workingOrdersRequest;
    return process_working_orders_request(workingOrdersRequest);

    }

int sample_test_client_moonx::process_working_orders_request(WORKING_ORDERS_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.wo_req = orderobject;

    f.hdr.msg_len = sizeof(WORKING_ORDERS_REQUEST);
    f.msg.wo_req.MessageType = mt1_working_orders_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " <<  f.msg.wo_req.MessageType                                      << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.wo_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.wo_req.UserTag                   
//     << '"' << "InstrumentID"                   << '"' << " : " << f.msg.wo_req.InstrumentID          
     << " }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(WORKING_ORDERS_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx working orders request failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return 0;
}


int sample_test_client_moonx::prompt_fills_request()
    {



FILL_REQUEST workingOrdersRequest={0};
    static FILL_REQUEST lastworkingOrdersRequest; 

    string strInput;

    prompt("UserId id ",workingOrdersRequest.UserID,lastworkingOrdersRequest.UserID);
    prompt("User Tag ",workingOrdersRequest.UserID,lastworkingOrdersRequest.UserTag);
    

    lastworkingOrdersRequest = workingOrdersRequest;
    return process_fills_request(workingOrdersRequest);

    }

int sample_test_client_moonx::process_fills_request(FILL_REQUEST&orderobject)
{
    BINARY_FRAME f;
    int ret = E_ev_no_error;

    f.msg.fill_req = orderobject;

    f.hdr.msg_len = sizeof(FILL_REQUEST);
    f.msg.fill_req.MessageType = mt1_fill_request;
    
     LOG(*m_log, m_name, logger::Info, "" 
     << '"' << "MessageLength"                  << '"' << " : " << f.hdr.msg_len                                     << ", "
     << '"' << "MessageType"                    << '"' << " : " <<  f.msg.fill_req.MessageType                                      << ", " 
     << '"' << "UserID"                         << '"' << " : " << f.msg.fill_req.UserID                     << ", " 
     << '"' << "UserTag"                        << '"' << " : " << f.msg.fill_req.UserTag                   
//     << '"' << "InstrumentID"                   << '"' << " : " << f.msg.wo_req.InstrumentID          
     << " }");
     
     LOG(*m_log, m_name, logger::Info, " Y/n ")
     char confirm;
     cin >>  confirm;
     if(confirm =='y' ||confirm =='Y')
     {
         if ((ret = tcp_write((char *)&f, sizeof(f.hdr) + sizeof(FILL_REQUEST))) != 0)
            LOG(*m_log, m_name, logger::Info, "tx working orders request failed ")
        else
        {
            //objStats.Set(f.msg);
            if (m_user_tags.insert(m_UserTag).second == false)
                LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            else
            {
                //LOG(*m_log, m_name, logger::Info, "insert into set failed " << m_UserTag)
            }
        }
     }
     else
     {
         LOG(*m_log, m_name, logger::Info, " cancelled ")
     }
    return 0;
}

int sample_test_client_moonx::prompt_asset_positions()
{
    cout << "wip"<<endl;
    return 0;
}
    int sample_test_client_moonx::prompt_symbols()
    {
        RestClient::Response r = RestClient::get("https://exchange.moonx.pro/exchangeApi/match/v-instrument");
        cout << r.body << endl;
        return 0;
    }
    int sample_test_client_moonx::prompt_depth()
    {
        cout << "wip"<<endl;
        return 0;
    }