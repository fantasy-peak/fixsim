#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include <quickfix/FixFieldNumbers.h>
#include <quickfix/Message.h>
#include <quickfix/Session.h>
#include <quickfix/fix40/ExecutionReport.h>
#include <quickfix/fix41/ExecutionReport.h>
#include <quickfix/fix42/ExecutionReport.h>
#include <quickfix/fix43/ExecutionReport.h>
#include <quickfix/fix44/ExecutionReport.h>
#include <quickfix/fix50/ExecutionReport.h>

#include <quickfix/fix40/OrderCancelReject.h>
#include <quickfix/fix41/OrderCancelReject.h>
#include <quickfix/fix42/OrderCancelReject.h>
#include <quickfix/fix43/OrderCancelReject.h>
#include <quickfix/fix44/OrderCancelReject.h>
#include <quickfix/fix50/OrderCancelReject.h>

#include <quickfix/fix42/TradingSessionStatus.h>
#include <quickfix/fix43/TradingSessionStatus.h>
#include <quickfix/fix44/TradingSessionStatus.h>
#include <quickfix/fix50/TradingSessionStatus.h>

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <asio/as_tuple.hpp>
#include <asio/post.hpp>
#include <pugixml.hpp>

#include "application.h"

namespace detail {

template <typename C>
struct to_helper {};

template <typename Container, std::ranges::range R>
    requires std::convertible_to<std::ranges::range_value_t<R>,
                                 typename Container::value_type>
Container operator|(R &&r, to_helper<Container>) {
    return Container{r.begin(), r.end()};
}

}  // namespace detail

template <std::ranges::range Container>
    requires(!std::ranges::view<Container>)
inline auto to() {
    return detail::to_helper<Container>{};
}

namespace {

auto spilt(const std::string &line, char flag) {
    return line | std::views::split(flag) |
           std::views::transform([](auto &&rng) {
               return std::string(&*rng.begin(), std::ranges::distance(
                                                     rng.begin(), rng.end()));
           }) |
           to<std::vector<std::string>>();
}

std::string getValue(const std::string &value) {
    auto ret =
        value | std::views::split('.') | std::views::transform([](auto &&rng) {
            return std::string_view(
                &*rng.begin(), std::ranges::distance(rng.begin(), rng.end()));
        }) |
        to<std::vector<std::string_view>>();
    if (ret.size() != 2) {
        SPDLOG_ERROR("invalid: {}", value);
        exit(1);
    }
    return std::string{ret[1]};
}

std::string getTzDateTime(std::string_view fmt = "{:%Y%m%d-%H:%M:%S}") {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto now_ms = time_point_cast<milliseconds>(now);
    auto tz = locate_zone("UTC");
    zoned_time zt{tz, now_ms};
    return std::vformat(fmt, std::make_format_args(zt));
}

std::string uuid() {
    uuid_t uuid;
    char uuid_str[37]{};
    uuid_generate_random(uuid);
    uuid_unparse(uuid, uuid_str);
    std::string value(uuid_str);
    std::ranges::replace(value, '-', '.');
    return value;
}

std::string randomNumber(int min = 1000, int max = 9999) {
    thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(min, max);
    return std::to_string(dist(gen));
}

std::string increment() {
    static std::atomic_uint64_t init_value = 1;
    return std::to_string(init_value.fetch_add(1, std::memory_order::relaxed));
}

}  // namespace

Application::Application(std::shared_ptr<asio::io_context> ctx,
                         const Config &cfg)
    : m_io_ctx(std::move(ctx)), m_cfg(cfg) {
    asio::co_spawn(*m_io_ctx, loopTimer(), asio::detached);
    asio::co_spawn(*m_io_ctx, clear(), asio::detached);
}

void Application::onCreate(const FIX::SessionID &id) {
    SPDLOG_INFO("onCreate: [{}]", id.toString());
    asio::post(m_pool,
               [this, id] { m_session = FIX::Session::lookupSession(id); });
}

void Application::onLogon(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogon: [{}]", id.toString());
    if (m_cfg.trading_session_status.empty())
        return;
    SPDLOG_INFO("start send Tss: [{}]", id.toString());
    asio::co_spawn(*m_io_ctx, sendTss(id), [](std::exception_ptr ep) {
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Caught exception in completion handler: {}",
                             e.what());
            }
        }
    });
}

void Application::onLogout(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogout: [{}]", id.toString());
}

void Application::toAdmin(FIX::Message &, const FIX::SessionID &) {}

void Application::toApp(FIX::Message &message, const FIX::SessionID &) {
    if (!m_cfg.header.has_value())
        return;
    for (auto &[tag, value] : m_cfg.header.value()) {
        if (value.starts_with("bool:")) {
            FIX::BoolField field(tag, value == "bool:true" ? true : false);
            message.getHeader().setField(field);
        } else {
            message.getHeader().setField(tag, value);
        }
    }
}

asio::awaitable<void> Application::sendCustomizeLoginResponse(
    FIX::Message msg, FIX::SessionID id) {
    SPDLOG_INFO("sendCustomizeLoginResponse: [{}]", id.toString());
    auto message = std::make_shared<FIX::Message>();
    message->getHeader().setField(
        FIX::MsgType(m_cfg.logon_response.value().msgtype));
    for (auto &[id, value] : m_cfg.logon_response.value().reply) {
        fillExecReport(message, msg, id, value);
    }
    FIX::Session::sendToTarget(*message, id);
    co_return;
}

void Application::fromAdmin(const FIX::Message &msg, const FIX::SessionID &id) {
    try {
        FIX::MsgType msgType;
        msg.getHeader().getField(msgType);
        if (msgType == FIX::MsgType_Logon && m_cfg.logon_response.has_value()) {
            asio::co_spawn(*m_io_ctx, sendCustomizeLoginResponse(msg, id),
                           [](std::exception_ptr ep) {
                               if (ep) {
                                   try {
                                       std::rethrow_exception(ep);
                                   } catch (const std::exception &e) {
                                       SPDLOG_ERROR(
                                           "fromAdmin Caught exception in "
                                           "completion handler: {}",
                                           e.what());
                                   }
                               }
                           });
        }
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
    }
}

void Application::fromApp(const FIX::Message &msg, const FIX::SessionID &id) {
    try {
        for (auto &[check_cond_header, check_cond_body, check_cl_order_id,
                    default_reply_flow, symbols_reply_flow] :
             m_cfg.custom_reply) {
            const auto &hdr = msg.getHeader();
            const auto &body = msg;

            bool header_match =
                std::ranges::all_of(check_cond_header, [&](const auto &cond) {
                    const auto &[field, expected] = cond;
                    auto value = hdr.getField(field);
                    if (expected == "optional(none)") {
                        return true;
                    }
                    return value == expected;
                });

            if (!header_match)
                continue;

            bool body_match =
                std::ranges::all_of(check_cond_body, [&](const auto &cond) {
                    const auto &[field, expected] = cond;
                    auto value = body.getField(field);
                    if (expected == "optional(none)") {
                        return true;
                    }
                    return value == expected;
                });

            if (!body_match)
                continue;

            auto msg_ptr = std::make_shared<FIX::Message>(msg);
            asio::post(*m_io_ctx, [id, this, &default_reply_flow,
                                   &symbols_reply_flow, &check_cl_order_id,
                                   msg_ptr = std::move(msg_ptr)]() mutable {
                std::string symbol;
                try {
                    auto &cl_ord_id = msg_ptr->getField(FIX::FIELD::ClOrdID);
                    // Check if order_id is duplicated
                    if (!check_cl_order_id.empty()) {
                        if (auto result = m_order_ids.insert(cl_ord_id);
                            !result.second) {
                            SPDLOG_INFO("duplicated order: {}", cl_ord_id);
                            static FixFieldMap map;
                            send(id, check_cl_order_id, map, *msg_ptr,
                                 MsgType::ExecutionReport);
                            return;
                        }
                    }
                    symbol = msg_ptr->getField(FIX::FIELD::Symbol);
                } catch (const std::exception &e) {
                    SPDLOG_ERROR("getField: {}", e.what());
                }

                if (auto it = std::ranges::find_if(
                        symbols_reply_flow,
                        [&](const auto &flow) {
                            return std::ranges::find(flow.symbols, symbol) !=
                                       flow.symbols.end() &&
                                   !flow.reply_flow.empty();
                        });
                    it != symbols_reply_flow.end()) {
                    addTimedTask(id, it->reply_flow, it->common_fields,
                                 msg_ptr);
                } else {
                    addTimedTask(id, default_reply_flow.reply_flow,
                                 default_reply_flow.common_fields, msg_ptr);
                }
            });

            return;
        }
        SPDLOG_ERROR("Configuration not matched: [{}]", msg.toString());
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
    }
}

void Application::addTimedTask(const FIX::SessionID &id,
                               std::vector<ReplyData> &reply_flow,
                               FixFieldMap &common_fix_fields,
                               const std::shared_ptr<FIX::Message> &msg_ptr) {
    std::chrono::milliseconds dut{0};
    for (auto &[fix_fields, interval, msg_type] : reply_flow) {
        if (interval < 0) {
            send(id, fix_fields, common_fix_fields, *msg_ptr, msg_type);
        } else {
            dut += std::chrono::milliseconds{interval};
            auto expiry = std::chrono::system_clock::now() + dut;
            m_timed.emplace(expiry,
                            TimedData{.id = id,
                                      .fix_fields = &fix_fields,
                                      .common_fix_fields = &common_fix_fields,
                                      .msg = msg_ptr,
                                      .msg_type = msg_type});
        }
    }
}

#define CREATE_FIX_MESSAGE_BY_VERSION(msg_type)         \
    switch (m_cfg.fix_version) {                        \
        case FixVersion::FIX40:                         \
            return std::make_shared<FIX40::msg_type>(); \
        case FixVersion::FIX41:                         \
            return std::make_shared<FIX41::msg_type>(); \
        case FixVersion::FIX42:                         \
            return std::make_shared<FIX42::msg_type>(); \
        case FixVersion::FIX43:                         \
            return std::make_shared<FIX43::msg_type>(); \
        case FixVersion::FIX44:                         \
            return std::make_shared<FIX44::msg_type>(); \
        case FixVersion::FIX50:                         \
            return std::make_shared<FIX50::msg_type>(); \
    }                                                   \
    throw std::runtime_error("Unsupported FIX version");

std::shared_ptr<FIX::Message> Application::createExecutionReport(){
    CREATE_FIX_MESSAGE_BY_VERSION(ExecutionReport)}

std::shared_ptr<FIX::Message> Application::createOrderCancelReject(){
    CREATE_FIX_MESSAGE_BY_VERSION(OrderCancelReject)}

std::shared_ptr<FIX::Message> Application::createTradingSessionStatus() {
    switch (m_cfg.fix_version) {
        case FixVersion::FIX40:
        case FixVersion::FIX41: {
            SPDLOG_DEBUG("error fix version");
            std::terminate();
        }
        case FixVersion::FIX42:
            return std::make_shared<FIX42::TradingSessionStatus>();
        case FixVersion::FIX43:
            return std::make_shared<FIX43::TradingSessionStatus>();
        case FixVersion::FIX44:
            return std::make_shared<FIX44::TradingSessionStatus>();
        case FixVersion::FIX50:
            return std::make_shared<FIX50::TradingSessionStatus>();
    }
    throw std::runtime_error("Unsupported FIX version");
}

void Application::setField(FIX::Message &message, int tag,
                           const std::string &value) {
    if (value == "bool:true" || value == "bool:false") {
        FIX::BoolField field(tag, value == "bool:true" ? true : false);
        message.setField(field);
    } else {
        message.setField(tag, value);
    }
}

void Application::fillExecReport(std::shared_ptr<FIX::Message> &message,
                                 const FIX::Message &msg, int field,
                                 const std::string &value) {
    if (value.starts_with("input.")) {
        auto val = getValue(value);
        setField(*message, field, msg.getField(std::stoi(val)));
    } else if (value.starts_with("if_input.")) {
        auto val = getValue(value);
        auto field = std::stoi(val);
        if (msg.isSetField(field)) {
            setField(*message, field, msg.getField(field));
        }
    } else if (value.starts_with("input_header.")) {
        auto val = getValue(value);
        setField(*message, field, msg.getHeader().getField(std::stoi(val)));
    } else if (value.starts_with("if_input_header.")) {
        auto val = getValue(value);
        auto field = std::stoi(val);
        if (msg.getHeader().isSetField(field)) {
            setField(*message, field, msg.getHeader().getField(field));
        }
    } else if (value.starts_with("call.")) {
        auto func_name = getValue(value);
        if (func_name == "uuid") {
            setField(*message, field, uuid());
        } else if (func_name == "getTzDateTime") {
            setField(*message, field, getTzDateTime());
        } else if (func_name == "randomNumber") {
            setField(*message, field, randomNumber());
        } else if (func_name == "increment") {
            setField(*message, field, increment());
        } else if (func_name == "createUniqueOrderID") {
            setField(*message, field, createUniqueOrderID(msg));
        } else {
            SPDLOG_ERROR("Unrecognized: {}", value);
        }
    } else {
        setField(*message, field, value);
    }
}

void Application::send(const FIX::SessionID &id, const FixFieldMap &fix_fields,
                       const FixFieldMap &common_fix_fields,
                       const FIX::Message &msg, MsgType msg_type) {
    try {
        std::shared_ptr<FIX::Message> message;
        if (msg_type == MsgType::ExecutionReport)
            message = createExecutionReport();
        else
            message = createOrderCancelReject();
        for (const auto &[field, value] : common_fix_fields) {
            fillExecReport(message, msg, field, value);
        }
        for (const auto &[field, value] : fix_fields) {
            fillExecReport(message, msg, field, value);
        }
        FIX::Session::sendToTarget(*message, id);
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
    }
}

asio::awaitable<void> Application::sendTss(FIX::SessionID id) {
    asio::steady_timer timer(*m_io_ctx);
    for (auto &[reply, interval] : m_cfg.trading_session_status) {
        auto message = createTradingSessionStatus();
        for (auto &[tag, value] : reply) {
            setField(*message, tag, value);
        }
        if (interval < 0) {
            FIX::Session::sendToTarget(*message, id);
            continue;
        }
        timer.expires_after(std::chrono::milliseconds(interval));
        auto [ec] =
            co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec)
            break;
        FIX::Session::sendToTarget(*message, id);
    }
}

asio::awaitable<void> Application::loopTimer() {
    asio::steady_timer timer(*m_io_ctx);
    for (;;) {
        timer.expires_after(std::chrono::milliseconds(m_cfg.interval));
        auto [ec] =
            co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec)
            break;
        if (m_pause.load(std::memory_order::relaxed)) {
            continue;
        }
        const auto now = std::chrono::system_clock::now();
        while (!m_timed.empty() && m_timed.begin()->first <= now) {
            auto data = std::move(m_timed.begin()->second);
            m_timed.erase(m_timed.begin());
            auto &[id, fix_fields, common_fix_fields, msg, msg_type] = data;
            send(id, *fix_fields, *common_fix_fields, *msg, msg_type);
        }
    }
}

asio::awaitable<void> Application::clear() {
    asio::steady_timer timer(*m_io_ctx);
    for (;;) {
        timer.expires_after(std::chrono::seconds(600));
        auto [ec] =
            co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec)
            break;
        auto now = std::chrono::system_clock::now();
        std::erase_if(m_ClOrdID_OrderID_mapping, [&](auto &p) mutable {
            auto &[point, str] = std::get<1>(p);
            auto dut = now - point;
            return dut > std::chrono::hours(24) ? true : false;
        });
    }
}

void Application::parseXml(const std::string &xml) {
    pugi::xml_document doc;
    if (!doc.load_file(xml.c_str())) {
        SPDLOG_ERROR("Failed to load XML: {}", xml);
        return;
    }
    pugi::xml_node fields = doc.child("fix").child("fields");
    for (pugi::xml_node field : fields.children("field")) {
        std::string name = field.attribute("name").as_string();
        int number = field.attribute("number").as_int();
        std::string type = field.attribute("type").as_string();
        nlohmann::json json;
        if (type == "CHAR" || type == "BOOLEAN") {
            for (pugi::xml_node value : field.children("value")) {
                std::string enum_val = value.attribute("enum").as_string();
                std::string desc = value.attribute("description").as_string();
                nlohmann::json enum_json;
                enum_json["value"] = enum_val;
                enum_json["description"] = desc;
                json["enum"].emplace_back(enum_json);
            }
        }
        m_tag_list[std::to_string(number)]["name"] = name;
        m_tag_list[std::to_string(number)]["type"] = type;
        json["tag"] = number;
        json["type"] = type;
        m_tag_mapping[name] = std::move(json);
    }
    auto parse_interface = [&](const std::string &interface_name) {
        nlohmann::json json;
        pugi::xml_node node =
            doc.child("fix")
                .child("messages")
                .find_child_by_attribute("message", "name",
                                         interface_name.c_str());
        if (node) {
            json["MsgType"] = node.attribute("msgtype").value();
            json["Name"] = node.attribute("name").value();
            for (pugi::xml_node field : node.children("field")) {
                auto name = field.attribute("name").value();
                auto &tag_json = m_tag_mapping[name];
                json["Field"][name]["tag"] = tag_json["tag"];
                json["Field"][name]["type"] = tag_json["type"];
                json["Field"][name]["required"] =
                    field.attribute("required").value();
                if (tag_json.contains("enum")) {
                    json["Field"][name]["enum"] = tag_json["enum"];
                }
            }
            for (pugi::xml_node field : node.children("group")) {
                auto name = field.attribute("name").value();
                auto &tag_json = m_tag_mapping[name];
                json["Field"][name]["tag"] = tag_json["tag"];
                json["Field"][name]["type"] = tag_json["type"];
                json["Field"][name]["required"] =
                    field.attribute("required").value();
            }
        }
        m_interface_mapping[interface_name] = json;
    };
    parse_interface("NewOrderSingle");
    parse_interface("OrderCancelReplaceRequest");
    parse_interface("OrderCancelRequest");
    parse_interface("ExecutionReport");
    parse_interface("OrderCancelReject");
    parse_interface("BusinessMessageReject");
    parse_interface("TradingSessionStatus");
}

void Application::startHttpServer() {
    auto http_server = std::make_shared<httplib::Server>();
    auto route = [&](const std::string &path) {
        auto handler = [this, path](const httplib::Request &req_path,
                                    httplib::Response &res) {
            if (req_path.path.ends_with("Yaml")) {
                std::stringstream ss_y;
                std::stringstream ss_n;
                for (auto &[key, value] :
                     m_interface_mapping[path]["Field"].items()) {
                    if (value["required"].get<std::string>() == "Y") {
                        ss_y << std::format(
                            "{}: {} # {}, required({}), type({})\n",
                            value["tag"].get<int>(), R"("")", key,
                            value["required"].get<std::string>(),
                            value["type"].get<std::string>());
                    } else {
                        ss_n << std::format(
                            "{}: {} # {}, required({}), type({})\n",
                            value["tag"].get<int>(), R"("")", key,
                            value["required"].get<std::string>(),
                            value["type"].get<std::string>());
                    }
                }
                res.set_content(ss_y.str() + ss_n.str(), "text/plain");
            } else {
                res.set_content(m_interface_mapping[path].dump(),
                                "application/json");
            }
        };
        http_server->Get(std::format("/{}", path), handler);
        http_server->Get(std::format("/{}Yaml", path), handler);
    };
    route("NewOrderSingle");
    route("OrderCancelReplaceRequest");
    route("OrderCancelRequest");
    route("ExecutionReport");
    route("OrderCancelReject");
    route("BusinessMessageReject");
    route("TradingSessionStatus");
    http_server->Get("/tag_list",
                     [this](const httplib::Request &, httplib::Response &res) {
                         res.set_content(m_tag_list.dump(), "application/json");
                     });
    // curl -X POST http://127.0.0.1:2025/pause -d '{"flag": true }'
    http_server->Post(
        "/pause", [this](const httplib::Request &req, httplib::Response &res) {
            try {
                auto j = nlohmann::json::parse(req.body);
                m_pause.store(j.at("flag").get<bool>());
                SPDLOG_INFO("pause: {}", m_pause ? "true" : "false");
            } catch (const std::exception &e) {
                SPDLOG_ERROR("{}", e.what());
                res.status = 400;
                res.set_content("invalid request", "text/plain");
                return;
            }
            res.set_content("success!\n", "text/plain");
        });
    http_server->Post(
        "/stress", [this](const httplib::Request &req, httplib::Response &res) {
            try {
                auto stress_data = spilt(req.body, '\n');
                SPDLOG_INFO("csv size: {}", stress_data.size());
                m_close_stress.store(false);
                asio::co_spawn(*m_io_ctx, startStress(std::move(stress_data)),
                               asio::detached);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("{}", e.what());
                res.status = 400;
                res.set_content("invalid request", "text/plain");
                return;
            }
            res.set_content("success!\n", "text/plain");
        });
    http_server->Get("/close/stress", [this](const httplib::Request &,
                                             httplib::Response &res) {
        m_close_stress.store(true);
        SPDLOG_INFO("close stress: {}", m_close_stress ? "true" : "false");
        res.set_content("success!\n", "text/plain");
    });
    m_thread = std::thread([http_server, this] {
        SPDLOG_INFO("start http server at {}:{}", m_cfg.http_server_host,
                    m_cfg.http_server_port);
        http_server->listen(m_cfg.http_server_host, m_cfg.http_server_port);
    });
    m_stop = [http_server] { http_server->stop(); };
}

void Application::stopHttpServer() {
    m_stop();
    if (m_thread.joinable())
        m_thread.join();
    m_pool.stop();
    m_pool.join();
}

asio::awaitable<void> Application::startStress(std::vector<std::string> csv) {
    if (csv.empty())
        co_return;
    asio::steady_timer timer(m_pool);
    uint64_t count = 0;
    std::vector<std::unordered_map<int32_t, std::string>> vec_fix_fields;
    for (const auto &line : csv) {
        std::unordered_map<int32_t, std::string> fix_fields;
        auto vec = spilt(line, ',');
        for (const auto &str : vec) {
            auto pair = spilt(str, '=');
            if (pair.size() != 2) {
                continue;
            }
            fix_fields.emplace(std::stoi(pair.at(0)), pair.at(1));
        }
        vec_fix_fields.emplace_back(std::move(fix_fields));
    }
    for (;;) {
        timer.expires_after(m_cfg.stress_interval);
        auto [ec] =
            co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec)
            break;
        if (m_close_stress.load(std::memory_order::relaxed)) {
            break;
        }
        for (const auto &fix_fields : vec_fix_fields) {
            auto exec_report = createExecutionReport();
            exec_report->setField(FIX::FIELD::TransactTime, getTzDateTime());
            for (auto &[tag, value] : fix_fields) {
                if (tag == FIX::FIELD::ExecID)
                    exec_report->setField(tag, std::to_string(count++));
                else
                    exec_report->setField(tag, value);
            }
            try {
                if (m_session)
                    m_session->send(*exec_report);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("{}", e.what());
            }
        }
    }
    co_return;
}

std::string Application::createUniqueOrderID(const FIX::Message &msg) {
    static std::atomic_uint64_t count = 1;
    try {
        auto &cl_ord_id = msg.getField(FIX::FIELD::ClOrdID);
        if (m_ClOrdID_OrderID_mapping.contains(cl_ord_id)) {
            const auto &[point, id] = m_ClOrdID_OrderID_mapping[cl_ord_id];
            return id;
        }
        auto order_id =
            std::format("fixsim.{}.{}", getTzDateTime("{:%Y%m%d.%H%M%S}"),
                        count.fetch_add(1, std::memory_order::relaxed));
        m_ClOrdID_OrderID_mapping.emplace(
            cl_ord_id,
            std::make_tuple(std::chrono::system_clock::now(), order_id));
        return order_id;
    } catch (const std::exception &e) {
        SPDLOG_ERROR("getField: {}", e.what());
        auto order_id =
            std::format("fixsim.{}.{}", getTzDateTime("{:%Y%m%d.%H%M%S}"),
                        count.fetch_add(1, std::memory_order::relaxed));
        return order_id;
    }
}
