#include "SpdlogProvider.hpp"
#include "Core/Facades/Runtime.hpp"
#include "Core/Stl.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <vector>

namespace
{
// Keep the log directory from growing without bound. Names are timestamped in a format
// that sorts chronologically as text, so sorting by filename is enough.
void PruneOldLogs(const std::filesystem::path& aDir, const std::wstring& aStem, size_t aKeep)
{
    std::error_code ec;
    std::vector<std::filesystem::path> logs;

    for (const auto& entry : std::filesystem::directory_iterator(aDir, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file(ec))
            continue;

        const auto name = entry.path().filename().wstring();

        if (name.starts_with(aStem + L"_") && entry.path().extension() == L".log")
            logs.push_back(entry.path());
    }

    if (logs.size() <= aKeep)
        return;

    std::sort(logs.begin(), logs.end());

    for (size_t i = 0; i < logs.size() - aKeep; ++i)
        std::filesystem::remove(logs[i], ec);
}
}

void Support::SpdlogProvider::OnInitialize()
{
    if (m_logPath.empty())
    {
        m_logPath = Core::Runtime::GetModulePath().replace_extension(L".log");
    }

    // One file per launch, kept in a logs/ subfolder.
    //
    // This sink is opened with truncate=true, so for as long as every session wrote to a
    // single CyberpunkMP.log, relaunching after a crash destroyed the only record of that
    // crash - precisely when the log is worth the most. A timestamped name per launch
    // means a crash log survives until pruning removes it, whether or not anything else
    // is watching.
    const auto logDir = m_logPath.parent_path() / L"logs";

    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    auto sessionPath = m_logPath;

    if (!ec)
    {
        const auto now = std::time(nullptr);
        std::tm parts{};

        if (localtime_s(&parts, &now) == 0)
        {
            wchar_t stamp[32]{};

            if (std::wcsftime(stamp, std::size(stamp), L"%Y-%m-%d_%H-%M-%S", &parts) > 0)
            {
                const auto stem = m_logPath.stem().wstring();

                sessionPath = logDir / (stem + L"_" + stamp + L".log");

                PruneOldLogs(logDir, stem, 20);
            }
        }
    }

    auto sink = Core::MakeShared<spdlog::sinks::basic_file_sink_mt>(sessionPath.wstring(), true);
    auto logger = Core::MakeShared<spdlog::logger>("", spdlog::sinks_init_list{sink});
    logger->flush_on(spdlog::level::trace);

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);

    SetDefault(*this);
}

void Support::SpdlogProvider::LogInfo(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->info(aMessage);
}

void Support::SpdlogProvider::LogWarning(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->warn(aMessage);
}

void Support::SpdlogProvider::LogError(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->error(aMessage);
}

void Support::SpdlogProvider::LogDebug(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->debug(aMessage);
}

void Support::SpdlogProvider::LogFlush()
{
    spdlog::default_logger_raw()->flush();
}
