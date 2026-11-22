#pragma once
#include <string>
#include <mutex>
#include <fstream>

namespace lsalt {

enum class LogLevel { Debug, Info, Warn, Error };

// Thread-safe, singleton file+debug-output logger. Writes to
// %LOCALAPPDATA%\LSAlt\logs\lsalt.log and OutputDebugString.
class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const { return m_level; }

    void Log(LogLevel level, const std::string& tag, const std::string& message);

    void Debug(const std::string& tag, const std::string& message) { Log(LogLevel::Debug, tag, message); }
    void Info(const std::string& tag, const std::string& message)  { Log(LogLevel::Info, tag, message); }
    void Warn(const std::string& tag, const std::string& message)  { Log(LogLevel::Warn, tag, message); }
    void Error(const std::string& tag, const std::string& message) { Log(LogLevel::Error, tag, message); }

    std::wstring GetLogFilePath() const { return m_logFilePath; }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex m_mutex;
    std::ofstream m_file;
    std::wstring m_logFilePath;
    LogLevel m_level = LogLevel::Info;
};

} // namespace lsalt
