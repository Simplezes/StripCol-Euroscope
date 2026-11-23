#include "pch.h"
#include "sector/SectorParser.h"
#include "common/Text.h"
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    std::vector<std::string> Split(const std::string &text, char separator)
    {
        std::vector<std::string> parts;
        std::stringstream stream(text);
        std::string part;
        while (std::getline(stream, part, separator))
            parts.push_back(Text::Trim(part));
        return parts;
    }

    std::vector<std::string> SplitWords(const std::string &text)
    {
        std::vector<std::string> words;
        std::stringstream stream(text);
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }

    bool StartsWith(const std::string &line, const char *prefix)
    {
        return line.rfind(prefix, 0) == 0;
    }

    void AddProcedure(const std::string &line, SectorData &result)
    {
        const bool isSid = StartsWith(line, "SID:");
        const std::vector<std::string> fields = Split(line, ':');
        if (fields.size() < 4 || fields[1].empty() || fields[2].empty() || fields[3].empty())
            return;

        const std::vector<std::string> waypoints = fields.size() > 4 ? SplitWords(fields[4]) : std::vector<std::string>();
        result.procedures[fields[1]][isSid ? "SID" : "STAR"][fields[2]][fields[3]] = waypoints;
    }

    class SectorBlockReader
    {
    public:
        void StartBlock()
        {
            Flush();
            inBlock_ = true;
        }

        void ReadLine(const std::string &line)
        {
            if (!inBlock_)
                return;

            if (StartsWith(line, "OWNER:"))
                owner_ = line.substr(6);
            else if (StartsWith(line, "DEPAPT:") || StartsWith(line, "ARRAPT:"))
                AddAirports(line.substr(7));
        }

        void Flush()
        {
            if (inBlock_ && !owner_.empty() && !airports_.empty())
                ownerAirports_[owner_].insert(airports_.begin(), airports_.end());

            inBlock_ = false;
            owner_.clear();
            airports_.clear();
        }

        void WriteTo(SectorData &result) const
        {
            for (const auto &[owner, airports] : ownerAirports_)
                result.sectors[owner]["airports"] = std::vector<std::string>(airports.begin(), airports.end());
        }

    private:
        void AddAirports(const std::string &list)
        {
            for (const std::string &airport : Split(list, ':'))
            {
                if (!airport.empty())
                    airports_.insert(airport);
            }
        }

        bool inBlock_ = false;
        std::string owner_;
        std::set<std::string> airports_;
        std::map<std::string, std::set<std::string>> ownerAirports_;
    };

    enum class Section
    {
        Other,
        ProceduresList,
        Airspace
    };

    Section SectionFor(const std::string &header)
    {
        if (header == "[SIDSSTARS]")
            return Section::ProceduresList;
        if (header == "[AIRSPACE]")
            return Section::Airspace;
        return Section::Other;
    }

    std::string StripComment(const std::string &line)
    {
        const size_t comment = line.find(';');
        return Text::Trim(comment == std::string::npos ? line : line.substr(0, comment));
    }
}

namespace SectorParser
{
    bool ParseFile(const std::string &path, SectorData &result)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        result = SectorData();
        SectorBlockReader sectorReader;
        Section section = Section::Other;
        std::string rawLine;

        while (std::getline(file, rawLine))
        {
            const std::string line = StripComment(rawLine);
            if (line.empty())
                continue;

            if (line.front() == '[')
            {
                sectorReader.Flush();
                section = SectionFor(line);
            }
            else if (section == Section::ProceduresList)
            {
                if (StartsWith(line, "SID:") || StartsWith(line, "STAR:"))
                    AddProcedure(line, result);
            }
            else if (section == Section::Airspace)
            {
                if (StartsWith(line, "SECTOR:"))
                    sectorReader.StartBlock();
                else
                    sectorReader.ReadLine(line);
            }
        }

        sectorReader.Flush();
        sectorReader.WriteTo(result);
        return result.HasProcedures();
    }
}
