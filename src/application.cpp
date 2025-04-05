#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>

#include <quickfix/Message.h>
#include <quickfix/Session.h>
#include <quickfix/fix40/ExecutionReport.h>
#include <quickfix/fix41/ExecutionReport.h>
#include <quickfix/fix42/ExecutionReport.h>
#include <quickfix/fix43/ExecutionReport.h>
#include <quickfix/fix44/ExecutionReport.h>
#include <quickfix/fix50/ExecutionReport.h>

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <asio/as_tuple.hpp>
#include <asio/post.hpp>
#include <pugixml.hpp>
#include <unordered_map>

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

std::string getTzDateTime() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto now_ms = time_point_cast<milliseconds>(now);
    auto tz = locate_zone("UTC");
    zoned_time zt{tz, now_ms};
    return std::format("{:%Y%m%d-%H:%M:%S}", zt);
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
}

void Application::onCreate(const FIX::SessionID &id) {
    SPDLOG_INFO("onCreate: [{}]", id.toString());
    asio::post(m_pool,
               [this, id] { m_session = FIX::Session::lookupSession(id); });
}

void Application::onLogon(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogon: [{}]", id.toString());
}

void Application::onLogout(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogout: [{}]", id.toString());
}

void Application::toAdmin(FIX::Message &, const FIX::SessionID &) {}

void Application::toApp(FIX::Message &, const FIX::SessionID &) {}

void Application::fromAdmin(const FIX::Message &, const FIX::SessionID &) {}

void Application::fromApp(const FIX::Message &msg, const FIX::SessionID &id) {
    try {
        std::string symbol;
        try {
            symbol = msg.getField(FIX::FIELD::Symbol);
        } catch (const std::exception &e) {
            SPDLOG_ERROR("get FIX::FIELD::Symbol error: {}", e.what());
        }
        for (auto &[check_cond_header, check_cond_body, default_reply_flow,
                    symbols_reply_flow] : m_cfg.custom_reply) {
            const auto &hdr = msg.getHeader();
            const auto &body = msg;

            bool header_match =
                std::ranges::all_of(check_cond_header, [&](const auto &cond) {
                    const auto &[field, expected] = cond;
                    return hdr.getField(field) == expected;
                });
            if (!header_match)
                continue;

            bool body_match =
                std::ranges::all_of(check_cond_body, [&](const auto &cond) {
                    const auto &[field, expected] = cond;
                    return body.getField(field) == expected;
                });
            if (!body_match)
                continue;

            auto msg_ptr = std::make_shared<FIX::Message>(msg);
            asio::post(*m_io_ctx, [id, this, &default_reply_flow,
                                   &symbols_reply_flow,
                                   symbol = std::move(symbol),
                                   msg_ptr = std::move(msg_ptr)]() mutable {
                if (auto it = std::ranges::find_if(
                        symbols_reply_flow,
                        [&](const auto &flow) {
                            return std::ranges::find(flow.symbols, symbol) !=
                                       flow.symbols.end() &&
                                   !flow.reply_flow.empty();
                        });
                    it != symbols_reply_flow.end()) {
                    addTimedTask(id, it->reply_flow, msg_ptr);
                } else {
                    addTimedTask(id, default_reply_flow, msg_ptr);
                }
            });

            return;
        }
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
    }
}

void Application::addTimedTask(const FIX::SessionID &id,
                               std::vector<ReplyData> &reply_flow,
                               const std::shared_ptr<FIX::Message> &msg_ptr) {
    for (auto &[reply, interval] : reply_flow) {
        if (interval <= 0) {
            send(id, reply, *msg_ptr);
        } else {
            auto expiry = std::chrono::system_clock::now() +
                          std::chrono::milliseconds{interval};
            m_timed.emplace(
                expiry, TimedData{.id = id, .reply = &reply, .msg = msg_ptr});
        }
    }
}

std::shared_ptr<FIX::Message> Application::createExecutionReport() {
    switch (m_cfg.fix_version) {
        case FixVersion::FIX40:
            return std::make_shared<FIX40::ExecutionReport>();
        case FixVersion::FIX41:
            return std::make_shared<FIX41::ExecutionReport>();
        case FixVersion::FIX42:
            return std::make_shared<FIX42::ExecutionReport>();
        case FixVersion::FIX43:
            return std::make_shared<FIX43::ExecutionReport>();
        case FixVersion::FIX44:
            return std::make_shared<FIX44::ExecutionReport>();
        case FixVersion::FIX50:
            return std::make_shared<FIX50::ExecutionReport>();
    }
    throw std::runtime_error("Unsupported FIX version");
}

void Application::send(const FIX::SessionID &id, const FixFieldMap &reply,
                       const FIX::Message &msg) {
    try {
        auto exec_report = createExecutionReport();
        for (const auto &[field, value] : reply) {
            if (value.starts_with("input.")) {
                auto val = getValue(value);
                exec_report->setField(field, msg.getField(std::stoi(val)));
            } else if (value.starts_with("call.")) {
                auto func_name = getValue(value);
                if (func_name == "uuid") {
                    exec_report->setField(field, uuid());
                } else if (func_name == "getTzDateTime") {
                    exec_report->setField(field, getTzDateTime());
                } else if (func_name == "randomNumber") {
                    exec_report->setField(field, randomNumber());
                } else if (func_name == "increment") {
                    exec_report->setField(field, increment());
                } else {
                    SPDLOG_ERROR("Unrecognized: {}", value);
                }
            } else {
                exec_report->setField(field, value);
            }
        }
        FIX::Session::sendToTarget(*exec_report, id);
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
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
            auto &[id, reply, msg] = data;
            send(id, *reply, *msg);
        }
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
        if (type == "CHAR") {
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
        }
        m_interface_mapping[interface_name] = json;
    };
    parse_interface("NewOrderSingle");
    parse_interface("OrderCancelReplaceRequest");
    parse_interface("OrderCancelRequest");
    parse_interface("ExecutionReport");
    parse_interface("OrderCancelReject");
    parse_interface("BusinessMessageReject");
}

void Application::startHttpServer() {
    auto http_server = std::make_shared<httplib::Server>();
    auto route = [&](const std::string &path) {
        http_server->Get(
            std::format("/{}", path),
            [this, path](const httplib::Request &, httplib::Response &res) {
                res.set_content(m_interface_mapping[path].dump(),
                                "application/json");
            });
    };
    route("NewOrderSingle");
    route("OrderCancelReplaceRequest");
    route("OrderCancelRequest");
    route("ExecutionReport");
    route("OrderCancelReject");
    route("BusinessMessageReject");
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
            exec_report->setField(60, getTzDateTime());
            for (auto &[tag, value] : fix_fields) {
                if (tag == 17)
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
