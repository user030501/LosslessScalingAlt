#include "Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace lsalt {

namespace {
std::wstring GetLogDirectory() {
    PWSTR path = nullptr;
    std::wstring result = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = std::wstring(path) + L"\\LSAlt\\logs";
        CoTaskMemFree(path);
    }
    return result;
}

const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}
} // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    std::wstring dir = GetLogDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    m_logFilePath = dir + L"\\lsalt.log";
    m_file.open(m_logFilePath, std::ios::app);
}

Logger::~Logger() {
    if (m_file.is_open()) m_file.close();
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

void Logger::Log(LogLevel level, const std::string& tag, const std::string& message) {
    if (level < m_level) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);

    std::ostringstream oss;
    oss << "[" << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S") << "] "
        << "[" << LevelToString(level) << "] "
        << "[" << tag << "] "
        << message << "\n";

    const std::string line = oss.str();

    if (m_file.is_open()) {
        m_file << line;
        m_file.flush();
    }

    OutputDebugStringA(line.c_str());
}

} // namespace lsalt
