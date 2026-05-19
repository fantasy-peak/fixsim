#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

#include "config.h"

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

    std::string createUniqueOrderID(const FIX::Message &);

private:
    void addTimedTask(const FIX::SessionID &, std::vector<ReplyData> &,
                      FixFieldMap &, const std::shared_ptr<FIX::Message> &);
    std::shared_ptr<FIX::Message> createTradingSessionStatus();
    void send(const FIX::SessionID &, const FixFieldMap &, const FixFieldMap &,
              const std::optional<std::vector<FixResponseGroup>> &,
              const FIX::Message &, const std::string &);
    asio::awaitable<void> loopTimer();
    asio::awaitable<void> startStress(std::vector<std::string>, std::string);
    asio::awaitable<void> sendTss(FIX::SessionID);
    asio::awaitable<void> clear();
    asio::awaitable<void> startPushJob(
        FIX::SessionID id, PushJob push_job,
        std::shared_ptr<asio::steady_timer> timer);
    void fillExecReport(std::shared_ptr<FIX::Message> &, const FIX::Message &,
                        int, const std::string &);
    asio::awaitable<void> sendCustomizeLoginResponse(FIX::Message,
                                                     FIX::SessionID);
    void addGroup(std::shared_ptr<FIX::Message> &message,
                  const FIX::Message &msg,
                  const FixResponseGroup &fix_response_group);

    std::shared_ptr<asio::io_context> m_io_ctx;
    Config m_cfg;
    std::unordered_map<std::string, FIX::Session *> m_sessions;
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<asio::steady_timer>>>
        m_push_jobs;

    struct TimedData {
        FIX::SessionID id;
        FixFieldMap *fix_fields;
        FixFieldMap *common_fix_fields;
        std::shared_ptr<FIX::Message> msg;
        std::string msg_type;
        std::optional<std::vector<FixResponseGroup>> response_groups;
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
