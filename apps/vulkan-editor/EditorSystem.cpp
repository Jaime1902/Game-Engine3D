#include "EditorSystem.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>

bool moveToRecycleBin(const std::filesystem::path& path) {
    // SHFileOperation necesita la ruta terminada en doble nulo.
    std::wstring from = std::filesystem::absolute(path).wstring();
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}

void revealInExplorer(const std::filesystem::path& path) {
    const std::wstring arguments = L"/select,\"" + std::filesystem::absolute(path).wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

#else
#include <system_error>

bool moveToRecycleBin(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::remove_all(path, error) > 0 && !error;
}

void revealInExplorer(const std::filesystem::path&) {}
#endif
