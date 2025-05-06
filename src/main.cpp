#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <quickfix/FileLog.h>
#include <quickfix/FileStore.h>
#include <quickfix/Log.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/SocketAcceptor.h>

#include <spdlog/spdlog.h>
#include <asio.hpp>

#include <application.h>

class SimFileLogFactory : public FIX::LogFactory {
public:
    SimFileLogFactory(FIX::SessionSettings settings)
        : m_settings(std::move(settings)),
          m_globalLog(nullptr),
          m_globalLogCount(0) {};
    SimFileLogFactory(const std::string &path)
        : m_path(path),
          m_backupPath(path),
          m_globalLog(nullptr),
          m_globalLogCount(0) {};
    SimFileLogFactory(std::string path, std::string backupPath)
        : m_path(std::move(path)),
          m_backupPath(std::move(backupPath)),
          m_globalLog(nullptr),
          m_globalLogCount(0) {};

public:
    FIX::Log *create() override;
    FIX::Log *create(const FIX::SessionID &) override;
    void destroy(FIX::Log *log) override;

private:
    std::string m_path;
    std::string m_backupPath;
    FIX::SessionSettings m_settings;
    FIX::Log *m_globalLog;
    int m_globalLogCount;
};

class SimFileLog : public FIX::Log {
public:
    SimFileLog(const std::string &path) { init(path, path, "GLOBAL"); }

    SimFileLog(const std::string &path, const std::string &backupPath) {
        init(path, backupPath, "GLOBAL");
    }

    SimFileLog(const std::string &path, const FIX::SessionID &s) {
        init(path, path, generatePrefix(s));
    }

    SimFileLog(const std::string &path, const std::string &backupPath,
               const FIX::SessionID &s) {
        init(path, backupPath, generatePrefix(s));
    }

    virtual ~SimFileLog() {
        m_messages.close();
        m_event.close();
    }

    void clear() override {
        m_messages.close();
        m_event.close();

        m_messages.open(m_messagesFileName.c_str(),
                        std::ios::out | std::ios::trunc);
        m_event.open(m_eventFileName.c_str(), std::ios::out | std::ios::trunc);
    }

    void backup() override {
        m_messages.close();
        m_event.close();

        int i = 0;
        while (true) {
            std::stringstream messagesFileName;
            std::stringstream eventFileName;

            messagesFileName << m_fullBackupPrefix << "messages.backup." << ++i
                             << ".log";
            eventFileName << m_fullBackupPrefix << "event.backup." << i
                          << ".log";
            FILE *messagesLogFile =
                FIX::file_fopen(messagesFileName.str().c_str(), "r");
            FILE *eventLogFile =
                FIX::file_fopen(eventFileName.str().c_str(), "r");

            if (messagesLogFile == nullptr && eventLogFile == nullptr) {
                FIX::file_rename(m_messagesFileName.c_str(),
                                 messagesFileName.str().c_str());
                FIX::file_rename(m_eventFileName.c_str(),
                                 eventFileName.str().c_str());
                m_messages.open(m_messagesFileName.c_str(),
                                std::ios::out | std::ios::trunc);
                m_event.open(m_eventFileName.c_str(),
                             std::ios::out | std::ios::trunc);
                return;
            }

            if (messagesLogFile != nullptr) {
                FIX::file_fclose(messagesLogFile);
            }
            if (eventLogFile != nullptr) {
                FIX::file_fclose(eventLogFile);
            }
        }
    }

    void onIncoming(const std::string &value) override {
        m_messages << FIX::UtcTimeStampConvertor::convert(
                          FIX::UtcTimeStamp::now(), 9)
                   << " I: " << value << std::endl;
    }
    void onOutgoing(const std::string &value) override {
        m_messages << FIX::UtcTimeStampConvertor::convert(
                          FIX::UtcTimeStamp::now(), 9)
                   << " O: " << value << std::endl;
    }
    void onEvent(const std::string &value) override {
        m_event << FIX::UtcTimeStampConvertor::convert(FIX::UtcTimeStamp::now(),
                                                       9)
                << " : " << value << std::endl;
    }

private:
    std::string generatePrefix(const FIX::SessionID &s) {
        const std::string &begin = s.getBeginString().getString();
        const std::string &sender = s.getSenderCompID().getString();
        const std::string &target = s.getTargetCompID().getString();
        const std::string &qualifier = s.getSessionQualifier();

        std::string prefix = begin + "-" + sender + "-" + target;
        if (qualifier.size()) {
            prefix += "-" + qualifier;
        }

        return prefix;
    }

    void init(std::string path, std::string backupPath,
              const std::string &prefix) {
        FIX::file_mkdir(path.c_str());
        FIX::file_mkdir(backupPath.c_str());

        if (path.empty()) {
            path = ".";
        }
        if (backupPath.empty()) {
            backupPath = path;
        }

        m_fullPrefix = FIX::file_appendpath(path, prefix + ".");
        m_fullBackupPrefix = FIX::file_appendpath(backupPath, prefix + ".");

        m_messagesFileName = m_fullPrefix + "messages.current.log";
        m_eventFileName = m_fullPrefix + "event.current.log";

        m_messages.open(m_messagesFileName.c_str(),
                        std::ios::out | std::ios::app);
        if (!m_messages.is_open()) {
            throw FIX::ConfigError("Could not open messages file: " +
                                   m_messagesFileName);
        }
        m_event.open(m_eventFileName.c_str(), std::ios::out | std::ios::app);
        if (!m_event.is_open()) {
            throw FIX::ConfigError("Could not open event file: " +
                                   m_eventFileName);
        }
    }
    std::ofstream m_messages;
    std::ofstream m_event;
    std::string m_messagesFileName;
    std::string m_eventFileName;
    std::string m_fullPrefix;
    std::string m_fullBackupPrefix;
};

FIX::Log *SimFileLogFactory::create() {
    if (++m_globalLogCount > 1) {
        return m_globalLog;
    }

    if (m_path.size()) {
        return new SimFileLog(m_path);
    }

    try {
        const FIX::Dictionary &settings = m_settings.get();
        std::string path = settings.getString(FIX::FILE_LOG_PATH);
        std::string backupPath = path;

        if (settings.has(FIX::FILE_LOG_BACKUP_PATH)) {
            backupPath = settings.getString(FIX::FILE_LOG_BACKUP_PATH);
        }

        return m_globalLog = new SimFileLog(path, backupPath);
    } catch (FIX::ConfigError &) {
        m_globalLogCount--;
        throw;
    }
}

FIX::Log *SimFileLogFactory::create(const FIX::SessionID &s) {
    if (m_path.size() && m_backupPath.size()) {
        return new SimFileLog(m_path, m_backupPath, s);
    }
    if (m_path.size()) {
        return new SimFileLog(m_path, s);
    }

    std::string path;
    std::string backupPath;
    FIX::Dictionary settings = m_settings.get(s);
    path = settings.getString(FIX::FILE_LOG_PATH);
    backupPath = path;
    if (settings.has(FIX::FILE_LOG_BACKUP_PATH)) {
        backupPath = settings.getString(FIX::FILE_LOG_BACKUP_PATH);
    }

    return new SimFileLog(path, backupPath, s);
}

void SimFileLogFactory::destroy(FIX::Log *pLog) {
    if (pLog != m_globalLog || --m_globalLogCount == 0) {
        delete pLog;
    }
}

int main(int argc, char **argv) {
    try {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#][%l] %v");
        auto [cfg, error] = yaml_cpp_struct::from_yaml<Config>(argv[1]);
        if (!cfg) {
            SPDLOG_ERROR("{}", error);
            return 1;
        }
        std::ranges::sort(cfg.value().custom_reply, [](auto &p1, auto &p2) {
            return (p1.check_condition_header.size() +
                    p1.check_condition_body.size()) >
                   (p2.check_condition_header.size() +
                    p2.check_condition_body.size());
        });
        auto [str, e] = yaml_cpp_struct::to_yaml(cfg.value());

        auto io_context = std::make_shared<asio::io_context>();
        asio::signal_set sig(*io_context, SIGINT, SIGTERM);
        SPDLOG_INFO("\n{}", str.value());

        FIX::SessionSettings settings(cfg.value().fix_ini);
        auto dict_file = settings.get().getString(FIX::DATA_DICTIONARY);
        SPDLOG_INFO("dictionary_file: {}", dict_file);

        Application application(io_context, cfg.value());
        application.parseXml(dict_file);
        application.startHttpServer();

        FIX::FileStoreFactory store_factory(settings);
        SimFileLogFactory log_factory(settings);

        auto acceptor = std::make_unique<FIX::SocketAcceptor>(
            application, store_factory, settings, log_factory);
        acceptor->start();

        sig.async_wait([&](const asio::error_code &, int) {
            SPDLOG_INFO("stop...");
            application.stopHttpServer();
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
