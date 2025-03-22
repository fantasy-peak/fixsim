#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <quickfix/Application.h>
#include <quickfix/FieldNumbers.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Mutex.h>
#include <quickfix/Utility.h>
#include <quickfix/Values.h>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <yaml_cpp_struct.hpp>

struct ReplyData {
    std::unordered_map<int32_t, std::string> reply;
    int32_t interval;
};
YCS_ADD_STRUCT(ReplyData, reply, interval)

struct SymbolsReplyData {
    std::vector<std::string> symbols;
    std::vector<ReplyData> reply_flow;
};
YCS_ADD_STRUCT(SymbolsReplyData, symbols, reply_flow)

struct Reply {
    std::unordered_map<int32_t, std::string> check_condition_header;
    std::unordered_map<int32_t, std::string> check_condition_body;
    std::vector<ReplyData> reply_flow;
    std::vector<SymbolsReplyData> symbols_reply_flow;
};
YCS_ADD_STRUCT(Reply, check_condition_header, check_condition_body, reply_flow,
               symbols_reply_flow)

struct Config {
    int32_t interval;
    std::string fix_ini;
    std::vector<Reply> custom_reply;
};
YCS_ADD_STRUCT(Config, interval, fix_ini, custom_reply)

class Application : public FIX::Application {
public:
    Application(std::shared_ptr<asio::io_context> ctx, const Config &cfg)
        : m_io_ctx(std::move(ctx)), m_cfg(cfg) {
        asio::co_spawn(*m_io_ctx, loopTimer(), asio::detached);
    }

    void onCreate(const FIX::SessionID &) override;
    void onLogon(const FIX::SessionID &) override;
    void onLogout(const FIX::SessionID &) override;
    void toAdmin(FIX::Message &, const FIX::SessionID &) override;
    void toApp(FIX::Message &, const FIX::SessionID &) override;
    void fromAdmin(const FIX::Message &, const FIX::SessionID &) override;
    void fromApp(const FIX::Message &, const FIX::SessionID &) override;

    void parseXml(const std::string &);

private:
    void addTimed(const FIX::SessionID &, const std::vector<ReplyData> &,
                  const std::shared_ptr<FIX::Message> &);
    void send(const FIX::SessionID &,
              const std::unordered_map<int32_t, std::string> &,
              const FIX::Message &);
    asio::awaitable<void> loopTimer();

    std::shared_ptr<asio::io_context> m_io_ctx;
    Config m_cfg;

    std::multimap<
        std::chrono::system_clock::time_point,
        std::tuple<FIX::SessionID, std::unordered_map<int32_t, std::string>,
                   std::shared_ptr<FIX::Message>>>
        m_timed;
    nlohmann::json m_tag_list;
    std::unordered_map<std::string, int32_t> m_tag_mapping;
    std::unordered_map<std::string, nlohmann::json> m_interface_mapping;
};

#endif
