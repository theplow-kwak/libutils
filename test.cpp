#include "argparser.hpp"
#include "logger.hpp"
#include <thread>
#include <locale>

// Use the argparse namespace
using namespace argparse;

std::string formatWithCommas(unsigned long number)
{
    std::string number_str = std::to_string(number);
    for (int i = number_str.length() - 3; i > 0; i -= 3)
        number_str.insert(i, ",");
    return number_str;
}

int main(int argc, char *argv[])
{
    Logger logger(LogLevel::DEBUG);
    // std::locale::global(std::locale(""));

    ArgParser parser("Copy and Compare test. ver. 0.1.0");
    parser.add_positional("command", "Command to excute.", true);
    parser.add_positional("source", "Source file or device path.", true);
    parser.add_option("-time", "-t", "test time (unit: min)", false, "2");
    parser.add_option("--dest", "-d", "destination directory path", true);
    parser.add_option("--thread", "-T", "thread count", false, "5");
    parser.add_option("--offset", "-o", "Start offset in hex for test", false, "0x1000"); // New option for hex test
    parser.add_flag("--test", "", "for test. used time unit as minute");
    parser.add_option("--log", "-L", "log level", false, "INFO");
    if (!parser.parse(argc, argv))
    {
        return 1;
    }

    auto cmd = parser.get_positional("command").value();
    auto source = parser.get_positional("source").value();
    std::vector<std::string> destlist = parser.get_list("dest").value_or(std::vector<std::string>{});
    
    // Use new get<T> for type conversion
    auto multithread = parser.get<int>("thread").value_or(1);
    auto test = parser.is_set("test");
    auto nTestTime = parser.get<int>("time").value_or(1) * ((test) ? 1 : 60);
    
    // Test hex parsing
    auto offset = parser.get<long>("offset").value_or(0);
    
    auto log_level = parser.get("log").value();
    logger.set_level(log_level);

    LOG_INFO(logger, "Source: {:>10}", source);
    LOG_INFO(logger, "Destination: {}", parser.get("dest").value());
    LOG_INFO(logger, "Thread count: {}", multithread);
    LOG_INFO(logger, "Offset: {:#x}", offset); // Log the parsed hex value
    LOG_INFO(logger, "Test mode: {}", test ? "enabled" : "disabled");
    LOG_INFO(logger, "Test time: {} minutes", formatWithCommas(nTestTime).c_str());
    printf("Test time: {%s} minutes\n", formatWithCommas(nTestTime).c_str());
    LOG_DEBUG(logger, "Destination count: {:04d}", destlist.size());
    for (const auto &dest : destlist)
    {
        LOG_DEBUG(logger, "Destination path: {}", dest);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG_INFO(logger, "Starting copy and compare test...");

    LOG_INFO(logger, "Copy and compare test completed.");
    return 0;
}
