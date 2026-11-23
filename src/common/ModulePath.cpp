#include "pch.h"
#include "common/ModulePath.h"

namespace ModulePath
{
    std::string Directory()
    {
        HMODULE module = nullptr;
        DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (!GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(&Directory), &module))
            return std::string();

        char path[MAX_PATH];
        DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return std::string();

        std::string fullPath(path, length);
        size_t separator = fullPath.find_last_of("\\/");
        return separator == std::string::npos ? std::string() : fullPath.substr(0, separator);
    }
}
