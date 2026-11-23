#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class ProcedureType
{
    Sid,
    Star
};

class ProcedureCatalog
{
public:
    bool Load(const std::string &path);

    const std::vector<std::string> &Waypoints(const std::string &airport,
                                              ProcedureType type,
                                              const std::string &runway,
                                              const std::string &name) const;

private:
    static std::string MakeKey(const std::string &airport,
                               ProcedureType type,
                               const std::string &runway,
                               const std::string &name);

    std::unordered_map<std::string, std::vector<std::string>> procedures_;
};
