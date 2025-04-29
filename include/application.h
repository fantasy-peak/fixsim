#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <quickfix/Application.h>
#include <quickfix/FieldNumbers.h>
#include <quickfix/Message.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Mutex.h>
#include <quickfix/Utility.h>
#include <quickfix/Values.h>

#include <asio.hpp>
#include <asio/thread_pool.hpp>
#include <nlohmann/json.hpp>
#include <yaml_cpp_struct.hpp>

using FixFieldMap = std::unordered_map<int32_t, std::string>;

enum class MsgType : uint8_t {
    ExecutionReport,
    OrderCancelReject,
};
YCS_ADD_ENUM(MsgType, ExecutionReport, OrderCancelReject)

enum class FixVersion : uint8_t {
    FIX40,
    FIX41,
    FIX42,
    FIX43,
    FIX44,
    FIX50,
};
YCS_ADD_ENUM(FixVersion, FIX40, FIX41, FIX42, FIX43, FIX44, FIX50)

struct TradingSessionStatus {
    FixFieldMap reply;
    int32_t interval;
};
YCS_ADD_STRUCT(TradingSessionStatus, reply, interval)

struct ReplyData {
    FixFieldMap reply;
    int32_t interval;
    MsgType msg_type;
};
YCS_ADD_STRUCT(ReplyData, reply, interval, msg_type)

struct SymbolsReplyData {
    FixFieldMap common_fields;
    std::vector<std::string> symbols;
    std::vector<ReplyData> reply_flow;
};
YCS_ADD_STRUCT(SymbolsReplyData, common_fields, symbols, reply_flow)

struct DefaultReplyData {
    FixFieldMap common_fields;
    std::vector<ReplyData> reply_flow;
};
YCS_ADD_STRUCT(DefaultReplyData, common_fields, reply_flow)

struct Reply {
    FixFieldMap check_condition_header;
    FixFieldMap check_condition_body;
    FixFieldMap check_cl_order_id;
    DefaultReplyData default_reply_flow;
    std::vector<SymbolsReplyData> symbols_reply_flow;
};
YCS_ADD_STRUCT(Reply, check_condition_header, check_condition_body,
               check_cl_order_id, default_reply_flow, symbols_reply_flow)

struct Config {
    FixVersion fix_version;
    std::string http_server_host;
    uint16_t http_server_port;
    int32_t interval;
    std::string fix_ini;
    std::chrono::microseconds stress_interval;
    std::vector<TradingSessionStatus> trading_session_status;
    std::vector<Reply> custom_reply;
};
YCS_ADD_STRUCT(Config, fix_version, http_server_host, http_server_port,
               interval, fix_ini, stress_interval, trading_session_status,
               custom_reply)

class Application : public FIX::Application {
public:
    Application(std::shared_ptr<asio::io_context>, const Config &);

    void onCreate(const FIX::SessionID &) override;
    void onLogon(const FIX::SessionID &) override;
    void onLogout(const FIX::SessionID &) override;
    void toAdmin(FIX::Message &, const FIX::SessionID &) override;
    void toApp(FIX::Message &, const FIX::SessionID &) override;
    void fromAdmin(const FIX::Message &, const FIX::SessionID &) override;
    void fromApp(const FIX::Message &, const FIX::SessionID &) override;

    void parseXml(const std::string &);
    void startHttpServer();
    void stopHttpServer();

private:
    void addTimedTask(const FIX::SessionID &, std::vector<ReplyData> &,
                      FixFieldMap &, const std::shared_ptr<FIX::Message> &);
    std::shared_ptr<FIX::Message> createExecutionReport();
    std::shared_ptr<FIX::Message> createOrderCancelReject();
    std::shared_ptr<FIX::Message> createTradingSessionStatus();
    void send(const FIX::SessionID &, const FixFieldMap &, const FixFieldMap &,
              const FIX::Message &, MsgType);
    asio::awaitable<void> loopTimer();
    asio::awaitable<void> startStress(std::vector<std::string>);
    asio::awaitable<void> sendTss(FIX::SessionID);
    void setField(FIX::Message &, int tag, const std::string &value);
    asio::awaitable<void> clear();
    std::string createUniqueOrderID(const FIX::Message &);

    std::shared_ptr<asio::io_context> m_io_ctx;
    Config m_cfg;
    asio::thread_pool m_pool{1};
    FIX::Session *m_session{nullptr};

    struct TimedData {
        FIX::SessionID id;
        FixFieldMap *fix_fields;
        FixFieldMap *common_fix_fields;
        std::shared_ptr<FIX::Message> msg;
        MsgType msg_type;
    };

    std::multimap<std::chrono::system_clock::time_point, TimedData> m_timed;

    std::thread m_thread;
    std::function<void()> m_stop = [] {};

    nlohmann::json m_tag_list;
    std::unordered_map<std::string, nlohmann::json> m_tag_mapping;
    std::unordered_map<std::string, nlohmann::json> m_interface_mapping;
    std::atomic_bool m_pause{false};
    std::atomic_bool m_close_stress{false};
    std::unordered_map<
        std::string,
        std::tuple<std::chrono::system_clock::time_point, std::string>>
        m_ClOrdID_OrderID_mapping;
    std::set<std::string> m_order_ids;
};

#endif
