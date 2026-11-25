#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

enum class LogLevel { info, warning, error };

struct LogEntry {
    LogLevel level{LogLevel::info};
    std::string time;
    std::string message;
    int repeats{1};
};

// Registro de mensajes que muestra la Consola del editor. Se puede escribir desde cualquier hilo
// (los mensajes de validación de Vulkan pueden llegar desde hilos del driver).
class EditorLog {
public:
    void add(LogLevel level, std::string message);
    void clear();
    [[nodiscard]] std::vector<LogEntry> entries() const;
    [[nodiscard]] std::array<int, 3> counts() const;
    [[nodiscard]] std::size_t revision() const;
    [[nodiscard]] LogEntry last() const;

private:
    static constexpr std::size_t capacity = 2000;
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::array<int, 3> counts_{};
    std::size_t revision_{};
};

EditorLog& editorLog();
