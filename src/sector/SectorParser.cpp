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

    struct TransferLine
    {
        std::string previous;
        std::string departureRunway;
        std::string fix;
        std::string next;
        std::string arrivalRunway;
        std::string fromSector;
        std::string toSector;
    };

    bool ReadTransfer(const std::string &line, TransferLine &transfer)
    {
        const std::vector<std::string> fields = Split(line, ':');
        if (fields.size() < 8 || fields[3].empty())
            return false;

        transfer = {fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7]};
        return true;
    }

    class SectorBlockReader
    {
    public:
        void StartBlock(const std::string &line)
        {
            Flush();
            inBlock_ = true;
            const std::vector<std::string> fields = Split(line, ':');
            name_ = fields.size() > 1 ? fields[1] : "";
        }

        void ReadLine(const std::string &line)
        {
            if (!inBlock_)
                return;

            if (StartsWith(line, "OWNER:"))
            {
                owner_ = line.substr(6);
                if (!name_.empty())
                    sectorOwners_[name_] = Split(owner_, ':');
            }
            else if (StartsWith(line, "DEPAPT:") || StartsWith(line, "ARRAPT:"))
                AddAirports(line.substr(7));
        }

        void Flush()
        {
            if (inBlock_ && !owner_.empty() && !airports_.empty())
                ownerAirports_[owner_].insert(airports_.begin(), airports_.end());

            inBlock_ = false;
            name_.clear();
            owner_.clear();
            airports_.clear();
        }

        std::vector<std::string> OwnersOf(const std::string &sector) const
        {
            auto found = sectorOwners_.find(sector);
            return found == sectorOwners_.end() ? std::vector<std::string>() : found->second;
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
        std::string name_;
        std::string owner_;
        std::map<std::string, std::vector<std::string>> sectorOwners_;
        std::set<std::string> airports_;
        std::map<std::string, std::set<std::string>> ownerAirports_;
    };

    enum class Section
    {
        Other,
        Positions,
        ProceduresList,
        Airspace
    };

    Section SectionFor(const std::string &header)
    {
        if (header == "[POSITIONS]")
            return Section::Positions;
        if (header == "[SIDSSTARS]")
            return Section::ProceduresList;
        if (header == "[AIRSPACE]")
            return Section::Airspace;
        return Section::Other;
    }

    void AddPosition(const std::string &line, SectorData &result)
    {
        const std::vector<std::string> fields = Split(line, ':');
        if (fields.size() < 7 || fields[3].empty())
            return;

        result.positions.push_back({{"callsign", fields[0]},
                                    {"frequency", fields[2]},
                                    {"id", fields[3]},
                                    {"suffix", fields[6]}});
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
        std::vector<TransferLine> transferLines;
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
            else if (section == Section::Positions)
            {
                AddPosition(line, result);
            }
            else if (section == Section::ProceduresList)
            {
                if (StartsWith(line, "SID:") || StartsWith(line, "STAR:"))
                    AddProcedure(line, result);
            }
            else if (section == Section::Airspace)
            {
                TransferLine transfer;
                if (StartsWith(line, "SECTOR:"))
                    sectorReader.StartBlock(line);
                else if ((StartsWith(line, "COPX:") || StartsWith(line, "FIR_COPX:")) && ReadTransfer(line, transfer))
                    transferLines.push_back(transfer);
                else
                    sectorReader.ReadLine(line);
            }
        }

        sectorReader.Flush();
        sectorReader.WriteTo(result);

        for (const TransferLine &transfer : transferLines)
        {
            result.transfers.push_back({{"previous", transfer.previous},
                                        {"departureRunway", transfer.departureRunway},
                                        {"fix", transfer.fix},
                                        {"next", transfer.next},
                                        {"arrivalRunway", transfer.arrivalRunway},
                                        {"fromOwners", sectorReader.OwnersOf(transfer.fromSector)},
                                        {"toOwners", sectorReader.OwnersOf(transfer.toSector)}});
        }
        return result.HasProcedures();
    }
}
