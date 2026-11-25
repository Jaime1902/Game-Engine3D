#include "EditorLog.hpp"

#include <ctime>
#include <iostream>
#include <utility>

namespace {
std::string currentTime() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%H:%M:%S", &local);
    return buffer;
}
}

void EditorLog::add(LogLevel level, std::string message) {
    (level == LogLevel::info ? std::cout : std::cerr) << message << '\n';
    std::lock_guard lock(mutex_);
    ++counts_[static_cast<std::size_t>(level)];
    ++revision_;
    // Mensajes idénticos seguidos se agrupan con un contador, como hace cualquier consola de editor.
    if (!entries_.empty() && entries_.back().level == level && entries_.back().message == message) {
        ++entries_.back().repeats;
        entries_.back().time = currentTime();
        return;
    }
    entries_.push_back({level, currentTime(), std::move(message), 1});
    while (entries_.size() > capacity) entries_.pop_front();
}

void EditorLog::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    counts_ = {};
    ++revision_;
}

std::vector<LogEntry> EditorLog::entries() const {
    std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

std::array<int, 3> EditorLog::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}

std::size_t EditorLog::revision() const {
    std::lock_guard lock(mutex_);
    return revision_;
}

LogEntry EditorLog::last() const {
    std::lock_guard lock(mutex_);
    return entries_.empty() ? LogEntry{} : entries_.back();
}

EditorLog& editorLog() {
    static EditorLog log;
    return log;
}
