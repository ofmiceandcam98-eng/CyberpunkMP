#include "ServerPCH.h"

#include "AuditLog.h"

#include <algorithm>   // std::transform, for the case-insensitive search
#include <cctype>      // std::tolower - transitive under MSVC, not under the GCC container
#include <chrono>
#include <deque>       // the ring that keeps only the newest matches
#include <random>

namespace
{
/**
 * Milliseconds since the epoch.
 *
 * Not a formatted timestamp. A ledger is read by tools far more often than by people, and
 * an integer sorts, subtracts and joins across instances without anyone having to agree on
 * a timezone - which two machines in different regions would otherwise have to.
 */
int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/**
 * A last-resort id for an instance nobody named.
 *
 * Short and random rather than a counter: counters restart at the same value on every
 * process, so two instances that were never given names would both call themselves "1" and
 * the ledger would be actively misleading rather than merely incomplete.
 */
std::string GenerateInstanceId()
{
    static const char* kDigits = "0123456789abcdef";

    std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<int> pick{0, 15};

    std::string id = "anon-";
    for (int i = 0; i < 8; ++i)
        id.push_back(kDigits[pick(engine)]);

    return id;
}
} // namespace

void AuditLog::Open(const std::filesystem::path& acPath)
{
    {
        std::lock_guard lock(m_mutex);

        if (const char* pNamed = std::getenv("NCO_INSTANCE_ID"); pNamed && *pNamed)
            m_instanceId = pNamed;
        else
            m_instanceId = GenerateInstanceId();

        std::error_code ec;
        std::filesystem::create_directories(acPath.parent_path(), ec);

        // Append, never truncate. The ledger outlives the process; a restart that wiped it
        // would destroy exactly the history someone restarted the server to investigate.
        m_stream.open(acPath, std::ios::out | std::ios::app);

        if (!m_stream.is_open())
        {
            // Not fatal. A server that refuses to start because it cannot write a ledger is a
            // worse outcome than one that runs without one - but this must be loud, because
            // the failure is otherwise silent until somebody goes looking for evidence that
            // was never being written.
            spdlog::error("Audit log could not be opened at {} - authoritative changes will NOT be recorded",
                          acPath.string());
            return;
        }

        m_open = true;
        m_path = acPath;   // retained so Search can read the ledger back

        spdlog::info("Audit log open at {} (instance '{}')", acPath.string(), m_instanceId);
    }

    // OUTSIDE the lock, because Record takes the same mutex itself. Holding it across
    // this call double-locked a non-recursive mutex on one thread - a self-deadlock
    // that hung every LINUX boot at exactly this point: the container printed the line
    // above and never reached Host(), so the test server sat "Up" for hours with its
    // port never bound. Windows happened to let the same code slide, which is how it
    // shipped.
    Record("server.start", {}, {}, {{"pid", static_cast<int64_t>(
#ifdef _WIN32
        ::GetCurrentProcessId()
#else
        ::getpid()
#endif
    )}});
}

void AuditLog::Record(const std::string& acAction,
                      const std::string& acActor,
                      const std::string& acSubject,
                      nlohmann::json aDetails)
{
    std::lock_guard lock(m_mutex);

    if (!m_open)
        return;

    nlohmann::json line{
        {"at", NowMs()},
        {"instance", m_instanceId},
        {"action", acAction},
    };

    // Absent rather than empty. A ledger full of "actor": "" is harder to filter than one
    // where the key simply is not there, and an empty string reads as a real value.
    if (!acActor.empty())
        line["actor"] = acActor;

    if (!acSubject.empty())
        line["subject"] = acSubject;

    if (!aDetails.empty())
        line["details"] = std::move(aDetails);

    // dump() with no indent - one object, one line, so the file stays greppable and
    // streamable. Flushed every time: see the header.
    m_stream << line.dump() << '\n';
    m_stream.flush();
}

void AuditLog::RecordMoney(const std::string& acActor,
                           const std::string& acSubject,
                           const std::string& acReason,
                           int64_t aBefore,
                           int64_t aAfter)
{
    Record("money.change", acActor, acSubject,
           {
               {"reason", acReason},
               {"before", aBefore},
               {"after", aAfter},
               {"delta", aAfter - aBefore},
           });
}

std::vector<std::string> AuditLog::Search(const std::string& acNeedle, size_t aMax)
{
    std::vector<std::string> found;

    // An empty needle would match every line ever written and hand back the tail of the
    // whole ledger, which is not a search - it is a way to page through other people's
    // business by accident.
    if (acNeedle.empty() || aMax == 0)
        return found;

    std::filesystem::path path;

    {
        std::lock_guard lock(m_mutex);

        if (!m_open)
            return found;

        // Flushed under the same lock that Record takes. Without this, an entry written
        // seconds ago can still be sitting in the buffer - and that is exactly the entry
        // somebody is searching for, because investigations start from what just happened.
        m_stream.flush();

        path = m_path;
    }

    // Opened separately for reading rather than seeking the write stream: the writer is in
    // append mode and shared between the game thread and the API queue, so moving its
    // position would be a data race for no benefit.
    std::ifstream file(path);

    if (!file.is_open())
        return found;

    const auto fold = [](std::string aText)
    {
        std::transform(aText.begin(), aText.end(), aText.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return aText;
    };

    const auto needle = fold(acNeedle);

    // A ring of the newest matches, not every match. The ledger is append-only and grows for
    // the life of the deployment, so collecting all of them then trimming would mean holding
    // an unbounded number of lines to hand back ten.
    std::deque<std::string> recent;
    std::string line;

    while (std::getline(file, line))
    {
        if (fold(line).find(needle) == std::string::npos)
            continue;

        recent.push_back(line);

        if (recent.size() > aMax)
            recent.pop_front();
    }

    // Newest first - the order an investigation reads in.
    found.assign(recent.rbegin(), recent.rend());

    return found;
}
