#include "pch.h"
#include "sector/SectorFileLocator.h"
#include "common/Constants.h"
#include "common/Text.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr int PARENT_FOLDERS_TO_SEARCH = 2;

    fs::path OverrideFile(const std::string &moduleDirectory)
    {
        return fs::path(moduleDirectory) / StripColConstants::SECTOR_PATH_FILE;
    }

    std::string ReadOverride(const std::string &moduleDirectory)
    {
        std::ifstream file(OverrideFile(moduleDirectory));
        std::string path;
        std::getline(file, path);
        return Text::Trim(path);
    }

    std::vector<fs::path> SearchFolders(const std::string &moduleDirectory)
    {
        std::vector<fs::path> folders;
        fs::path folder(moduleDirectory);
        for (int level = 0; level <= PARENT_FOLDERS_TO_SEARCH && !folder.empty(); ++level)
        {
            folders.push_back(folder);
            folder = folder.parent_path();
        }
        return folders;
    }

    bool IsSectorExtensionFile(const fs::directory_entry &entry)
    {
        std::error_code error;
        if (!entry.is_regular_file(error))
            return false;
        return Text::ToLower(entry.path().extension().string()) == ".ese";
    }

    std::string NewestSectorFile(const std::vector<fs::path> &folders)
    {
        fs::path newest;
        fs::file_time_type newestTime;

        for (const fs::path &folder : folders)
        {
            std::error_code error;
            for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
            {
                if (!IsSectorExtensionFile(*it))
                    continue;

                const fs::file_time_type time = it->last_write_time(error);
                if (error)
                    continue;
                if (newest.empty() || time > newestTime)
                {
                    newest = it->path();
                    newestTime = time;
                }
            }
        }
        return newest.string();
    }
}

namespace SectorFileLocator
{
    std::string Find(const std::string &moduleDirectory)
    {
        const std::string configured = ReadOverride(moduleDirectory);
        if (!configured.empty())
            return configured;

        return NewestSectorFile(SearchFolders(moduleDirectory));
    }

    bool SaveOverride(const std::string &moduleDirectory, const std::string &path)
    {
        std::ofstream file(OverrideFile(moduleDirectory), std::ios::trunc);
        if (!file.is_open())
            return false;

        file << path;
        return true;
    }
}
