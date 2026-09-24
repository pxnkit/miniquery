#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

using Totals = std::map<std::string, std::int64_t>;
struct Columns { std::vector<std::string> customer; std::vector<std::int64_t> amount; };

std::int64_t integer(const std::string& value) {
    if (!std::regex_match(value, std::regex("-?[0-9]+"))) throw std::runtime_error("Invalid integer: " + value);
    std::size_t used = 0;
    auto number = std::stoll(value, &used);
    if (used != value.size()) throw std::runtime_error("Invalid integer");
    return number;
}
void add(std::int64_t& target, std::int64_t value) {
    if ((value > 0 && target > std::numeric_limits<std::int64_t>::max() - value) ||
        (value < 0 && target < std::numeric_limits<std::int64_t>::min() - value))
        throw std::runtime_error("SUM overflow");
    target += value;
}
Columns load(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open input file");
    std::string line;
    auto trimCR = [](std::string& text) { if (!text.empty() && text.back() == '\r') text.pop_back(); };
    if (!std::getline(file, line)) throw std::runtime_error("Missing CSV header");
    trimCR(line);
    if (line != "customer,amount_cents") throw std::runtime_error("Expected CSV header: customer,amount_cents");
    Columns data;
    const std::regex key("[A-Za-z0-9_-]+");
    while (std::getline(file, line)) {
        trimCR(line);
        auto comma = line.find(',');
        if (comma == std::string::npos || line.find(',', comma + 1) != std::string::npos)
            throw std::runtime_error("Expected two unquoted CSV columns");
        auto name = line.substr(0, comma);
        if (!std::regex_match(name, key)) throw std::runtime_error("Customer must contain only letters, digits, underscores or hyphens");
        data.customer.push_back(name);
        data.amount.push_back(integer(line.substr(comma + 1)));
    }
    if (file.bad()) throw std::runtime_error("Input read failed");
    return data;
}
int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: miniquery FILE.csv SQL [--threads N] [--explain]\n";
            return 2;
        }
        std::size_t threads = 1;
        bool explain = false;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--explain") explain = true;
            else if (arg == "--threads" && i + 1 < argc) {
                auto n = integer(argv[++i]);
                if (n < 1 || n > 64) throw std::runtime_error("Threads must be between 1 and 64");
                threads = static_cast<std::size_t>(n);
            } else throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
        const std::regex grammar(
            R"(^\s*SELECT\s+customer\s*,\s*SUM\s*\(\s*amount_cents\s*\)\s+FROM\s+orders(?:\s+WHERE\s+amount_cents\s*>=\s*(-?[0-9]+))?\s+GROUP\s+BY\s+customer\s*;?\s*$)",
            std::regex::icase);
        std::smatch match;
        std::string sql = argv[2];
        if (!std::regex_match(sql, match, grammar)) throw std::runtime_error("Unsupported SQL; see README for the exact supported grammar");
        std::optional<std::int64_t> minimum;
        if (match[1].matched) minimum = integer(match[1].str());
        auto start = std::chrono::steady_clock::now();
        auto data = load(argv[1]);
        auto loaded = std::chrono::steady_clock::now();
        auto workers = std::min(threads, std::max<std::size_t>(1, data.amount.size()));
        std::vector<std::future<Totals>> tasks;
        for (std::size_t worker = 0; worker < workers; ++worker) {
            auto begin = data.amount.size() * worker / workers;
            auto end = data.amount.size() * (worker + 1) / workers;
            tasks.push_back(std::async(std::launch::async, [&, begin, end] {
                Totals local;
                for (auto row = begin; row < end; ++row)
                    if (!minimum || data.amount[row] >= *minimum) add(local[data.customer[row]], data.amount[row]);
                return local;
            }));
        }
        Totals result;
        for (auto& task : tasks) for (const auto& [key, value] : task.get()) add(result[key], value);
        auto finished = std::chrono::steady_clock::now();
        if (explain) {
            std::cerr << "CSV scan -> " << (minimum ? "filter -> " : "") << "partitioned aggregate (" << workers << " workers) -> merge\n";
            std::cerr << "rows=" << data.amount.size() << " groups=" << result.size()
                << " load_ms=" << std::chrono::duration<double, std::milli>(loaded - start).count()
                << " execute_ms=" << std::chrono::duration<double, std::milli>(finished - loaded).count() << '\n';
        }
        std::cout << "customer,total_cents\n";
        for (const auto& [key, value] : result) std::cout << key << ',' << value << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
