#include "CsvTable.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ctltest {

namespace {

std::string formatError(const std::string& path, size_t line, const std::string& msg) {
    std::ostringstream os;
    os << path << ":" << line << ": " << msg;
    return os.str();
}

// Split one logical CSV line into cells, honoring "..." quoting and "" escapes.
std::vector<std::string> splitRow(const std::string& line, const std::string& path, size_t lineno) {
    std::vector<std::string> cells;
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuote) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    ++i;
                } else {
                    inQuote = false;
                }
            } else {
                cur.push_back(c);
            }
        } else {
            if (c == ',') {
                cells.push_back(std::move(cur));
                cur.clear();
            } else if (c == '"') {
                if (!cur.empty()) {
                    throw CsvError(path, lineno, "stray quote in unquoted field");
                }
                inQuote = true;
            } else {
                cur.push_back(c);
            }
        }
    }
    if (inQuote) {
        throw CsvError(path, lineno, "unterminated quoted field");
    }
    cells.push_back(std::move(cur));
    return cells;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

} // namespace

CsvError::CsvError(const std::string& path, size_t line, const std::string& msg)
    : std::runtime_error(formatError(path, line, msg)), _path(path), _line(line) {}

size_t CsvTable::colIndex(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); ++i) if (columns[i] == name) return i;
    return static_cast<size_t>(-1);
}

CsvTable readCsv(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw CsvError(path, 0, "cannot open file");

    CsvTable t;
    t.path = path;

    std::string line;
    size_t lineno = 0;
    bool gotHeader = false;
    size_t ncols = 0;
    while (std::getline(f, line)) {
        ++lineno;
        // Strip trailing CR from CRLF input.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;

        std::vector<std::string> cells = splitRow(line, path, lineno);
        if (!gotHeader) {
            for (auto& c : cells) c = trim(c);
            t.columns = std::move(cells);
            ncols = t.columns.size();
            if (ncols == 0) throw CsvError(path, lineno, "header row is empty");
            gotHeader = true;
        } else {
            if (cells.size() != ncols) {
                std::ostringstream os;
                os << "row has " << cells.size() << " cells, expected " << ncols;
                throw CsvError(path, lineno, os.str());
            }
            t.rows.push_back(std::move(cells));
        }
    }
    if (!gotHeader) throw CsvError(path, 0, "empty CSV file (no header row)");
    return t;
}

Value cellToValue(const std::string& raw) {
    const std::string s = trim(raw);
    if (s.empty()) return Value::makeString(s);

    // bool
    if (s == "true"  || s == "True"  || s == "TRUE")  return Value::makeBool(true);
    if (s == "false" || s == "False" || s == "FALSE") return Value::makeBool(false);

    // integer (no decimal or exponent)
    if (s.find('.') == std::string::npos
     && s.find('e') == std::string::npos
     && s.find('E') == std::string::npos)
    {
        char* end = nullptr;
        errno = 0;
        long long iv = std::strtoll(s.c_str(), &end, 10);
        if (errno == 0 && end && *end == '\0') {
            return Value::makeInt(static_cast<int64_t>(iv));
        }
    }

    // float
    {
        char* end = nullptr;
        errno = 0;
        double fv = std::strtod(s.c_str(), &end);
        if (errno == 0 && end && *end == '\0') {
            return Value::makeFloat(fv);
        }
    }

    return Value::makeString(s);
}

std::vector<std::map<std::string, Value>> tableAsRows(const CsvTable& t) {
    std::vector<std::map<std::string, Value>> out;
    out.reserve(t.rows.size());
    for (const auto& row : t.rows) {
        std::map<std::string, Value> m;
        for (size_t c = 0; c < t.columns.size() && c < row.size(); ++c) {
            m.emplace(t.columns[c], cellToValue(row[c]));
        }
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace ctltest
