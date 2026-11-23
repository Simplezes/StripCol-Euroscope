#pragma once

#include <string>

namespace SectorFileLocator
{
    std::string Find(const std::string &moduleDirectory);
    bool SaveOverride(const std::string &moduleDirectory, const std::string &path);
}
