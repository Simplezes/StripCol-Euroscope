#include "pch.h"
#include "common/Log.h"

namespace Log
{
    void Write(const std::string &message)
    {
        OutputDebugStringA(("[StripCol] " + message + "\n").c_str());
    }
}
