#include <memory>
#include <string>

#include <quickfix/FileLog.h>
#include <quickfix/FileStore.h>
#include <quickfix/Log.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/SocketAcceptor.h>

#include <spdlog/spdlog.h>
#include <asio.hpp>

#include <application.h>

int main(int argc, char **argv) {
    try {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#][%l] %v");
        auto [cfg, error] = yaml_cpp_struct::from_yaml<Config>(argv[1]);
        if (!cfg) {
            SPDLOG_ERROR("{}", error);
            return 1;
        }
        auto [str, e] = yaml_cpp_struct::to_yaml(cfg.value());

        auto io_context = std::make_shared<asio::io_context>();
        asio::signal_set sig(*io_context, SIGINT, SIGTERM);
        SPDLOG_INFO("\n{}", str.value());

        FIX::SessionSettings settings(cfg.value().fix_ini);
        auto dict_file = settings.get().getString(FIX::DATA_DICTIONARY);
        SPDLOG_INFO("dictionary_file: {}", dict_file);

        Application application(io_context, cfg.value());
        application.parseXml(dict_file);

        FIX::FileStoreFactory store_factory(settings);
        FIX::FileLogFactory log_factory(settings);

        auto acceptor = std::make_unique<FIX::SocketAcceptor>(
            application, store_factory, settings, log_factory);
        acceptor->start();

        sig.async_wait([&](const asio::error_code &, int) {
            acceptor->stop();
            io_context->stop();
        });
        io_context->run();
        return 0;
    } catch (const std::exception &e) {
        SPDLOG_ERROR("{}", e.what());
        return 1;
    }
}
