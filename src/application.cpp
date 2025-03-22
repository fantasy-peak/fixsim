#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <tuple>

#include <quickfix/Message.h>
#include <quickfix/Session.h>
#include <quickfix/fix42/ExecutionReport.h>

#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <asio/as_tuple.hpp>
#include <asio/post.hpp>
#include <pugixml.hpp>

#include "application.h"

namespace {

auto split(const std::string &value) {
    return value | std::views::split('.') |
           std::views::transform([](auto &&rng) {
               return std::string(&*rng.begin(), std::ranges::distance(
                                                     rng.begin(), rng.end()));
           }) |
           std::ranges::to<std::vector<std::string>>();
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

}  // namespace

void Application::onCreate(const FIX::SessionID &id) {
    SPDLOG_INFO("onCreate: {}", id.toString());
}

void Application::onLogon(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogon: [{}]", id.toString());
}

void Application::onLogout(const FIX::SessionID &id) {
    SPDLOG_INFO("onLogout: [{}]", id.toString());
}

void Application::toAdmin(FIX::Message &message, const FIX::SessionID &id) {}

void Application::toApp(FIX::Message &message, const FIX::SessionID &id) {}

void Application::fromAdmin(const FIX::Message &message,
                            const FIX::SessionID &id) {}

void Application::fromApp(const FIX::Message &msg, const FIX::SessionID &id) {
    try {
        std::string symbol;
        try {
            symbol = msg.getField(FIX::FIELD::Symbol);
        } catch (const std::exception &e) {
            SPDLOG_ERROR("get FIX::FIELD::Symbol error: {}", e.what());
        }
        for (const auto &[check_cond_header, check_cond_body, reply_flow,
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

            // for (auto [k, v] : check_cond_body)
            //     SPDLOG_INFO("{}-{}", k, v);
            auto msg_ptr = std::make_shared<FIX::Message>(msg);
            asio::post(*m_io_ctx, [id, this, &reply_flow, &symbols_reply_flow,
                                   symbol = std::move(symbol),
                                   msg_ptr = std::move(msg_ptr)]() mutable {
                for (const auto &flow : symbols_reply_flow) {
                    auto it = std::ranges::find_if(
                        flow.symbols,
                        [&](const auto &p) { return p == symbol; });
                    if (it != flow.symbols.end() && !flow.reply_flow.empty()) {
                        addTimed(id, flow.reply_flow, msg_ptr);
                        return;
                    }
                }
                addTimed(id, reply_flow, msg_ptr);
            });
            return;
        }
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
    }
}

void Application::addTimed(const FIX::SessionID &id,
                           const std::vector<ReplyData> &reply_flow,
                           const std::shared_ptr<FIX::Message> &msg_ptr) {
    for (auto &[reply, interval] : reply_flow) {
        if (interval <= 0) {
            send(id, reply, *msg_ptr);
        } else {
            auto expiry = std::chrono::system_clock::now() +
                          std::chrono::milliseconds(interval);
            m_timed.emplace(expiry, std::make_tuple(id, reply, msg_ptr));
        }
    }
}

void Application::send(const FIX::SessionID &id,
                       const std::unordered_map<int32_t, std::string> &reply,
                       const FIX::Message &msg) {
    try {
        FIX42::ExecutionReport execution_report;
        for (const auto &[field, value] : reply) {
            if (value.starts_with("InputField.")) {
                auto vec = split(value);
                execution_report.setField(field,
                                          msg.getField(std::stoul(vec.at(1))));
                continue;
            }
            if (value.starts_with("CallFunc.")) {
                auto vec = split(value);
                const auto &func_name = vec[1];
                if (func_name == "uuid")
                    execution_report.setField(field, uuid());
                else if (func_name == "getTzDateTime") {
                    execution_report.setField(field, getTzDateTime());
                } else {
                    SPDLOG_ERROR("Unrecognized: {}", value);
                }
                continue;
            }
            execution_report.setField(field, value);
        }
        FIX::Session::sendToTarget(execution_report, id);
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
        const auto now = std::chrono::system_clock::now();
        while (!m_timed.empty() && m_timed.begin()->first <= now) {
            auto data = std::move(m_timed.begin()->second);
            m_timed.erase(m_timed.begin());
            auto &[id, reply, msg] = data;
            send(id, reply, *msg);
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
        m_tag_list[std::to_string(number)] = std::format("{} ({})", name, type);
        m_tag_mapping[name] = number;
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
                json["Field"][name] =
                    std::format("tag({}) Required({})", m_tag_mapping[name],
                                field.attribute("required").value());
            }
        }
        m_interface_mapping[interface_name] = json;
        std::ofstream out("interface.txt", std::ios::app);
        if (!out) {
            SPDLOG_ERROR("can't open ./interface.txt");
            return;
        }
        out << json.dump(2) << "\n";
    };
    parse_interface("NewOrderSingle");
    parse_interface("OrderCancelReplaceRequest");
    parse_interface("OrderCancelRequest");
    parse_interface("ExecutionReport");
    parse_interface("OrderCancelReject");
    parse_interface("BusinessMessageReject");
}
