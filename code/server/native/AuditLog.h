#pragma once

/**
 * An append-only record of every authoritative change the server makes.
 *
 * WHY THIS EXISTS
 *
 * Money has moved without anyone being able to say how. The 20000 -> 300 -> 20000 pattern
 * was visible in player reports for weeks and could not be attributed, because the only
 * evidence was spdlog lines written for human reading, interleaved with everything else,
 * and never in a form anything could search. "Whose balance changed, when, because of what
 * request" was not answerable after the fact.
 *
 * This is that record. One line per authoritative change, machine-readable, written the
 * moment the change is made rather than reconstructed afterwards.
 *
 * WHAT BELONGS HERE
 *
 * State the server owns and a player would be upset to lose or delighted to forge: money,
 * items, vehicles, ownership transfers, admin actions, bans, character deletion. Not chat,
 * not movement, not anything at frame rate - this is a ledger, not a trace log.
 *
 * REPLICABLE INSTANCES
 *
 * The standing rule is that a second instance must be able to run without a rewrite, so
 * every line carries an instance id. Without it, two instances writing ledgers is worse
 * than one writing none: the lines interleave when collected and nothing says which
 * machine made which change.
 *
 * The file is deliberately local for now. A ledger is a RECORD, not authoritative state -
 * nothing reads it back to make a decision - so it does not carry the divergence risk that
 * players.json does. When persistence moves to a database, this moves with it, and the
 * instance id is already the column that makes the merge meaningful.
 *
 * FORMAT
 *
 * Newline-delimited JSON. One object per line, appended and flushed immediately: a ledger
 * that loses its last entries in a crash is useless precisely when it is needed, which is
 * after something went wrong.
 */

#include <string>
#include <vector>      // Search returns matching lines
#include <mutex>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>

class AuditLog
{
public:
    /**
     * Opens the ledger and settles this process's instance id.
     *
     * The id comes from NCO_INSTANCE_ID when the deployment sets one - compose can give
     * each instance a stable name, which is what makes collected ledgers readable. Without
     * it, a per-process id is generated so lines are still attributable; it just will not
     * survive a restart, which is the honest outcome when nobody has named the instance.
     */
    void Open(const std::filesystem::path& acPath);

    /**
     * Record one authoritative change.
     *
     * `acAction` is a stable, greppable verb - "money.transfer", "item.grant",
     * "vehicle.sell", "admin.command". Dotted rather than free text so that a whole class
     * of events can be pulled out with one prefix.
     *
     * `acActor` is who caused it, by Discord id, because that is the identity that survives
     * a reconnect and a move between machines. `acSubject` is who it happened TO, which is
     * usually but not always the same person - an admin granting money is the clearest
     * case where it is not.
     *
     * `aDetails` carries whatever the action needs. Deliberately free-form: constraining it
     * now would mean guessing the shape of phases that have not been built.
     */
    void Record(const std::string& acAction,
                const std::string& acActor,
                const std::string& acSubject,
                nlohmann::json aDetails = nlohmann::json::object());

    /**
     * Money changed. The one that prompted all this, so it gets a named helper rather than
     * relying on every call site to remember the same field names.
     *
     * Both balances are recorded, not just the delta. A delta alone cannot answer "did the
     * server and the client disagree about the starting balance", which is the actual
     * question behind the thrash.
     */
    void RecordMoney(const std::string& acActor,
                     const std::string& acSubject,
                     const std::string& acReason,
                     int64_t aBefore,
                     int64_t aAfter);

    /**
     * Read the ledger back - the newest entries whose line contains `acNeedle`.
     *
     * WHY THIS EXISTS. The ledger was write-only: everything worth investigating was being
     * recorded faithfully and there was no way to look at any of it without shell access to
     * the box. That is half an audit trail. The trade brief asks for it directly ("allow
     * staff to search TradeID, CharacterID, ... this will be extremely useful for staff
     * investigating exploits"), and an exploit report is answered in minutes or not at all.
     *
     * A SUBSTRING MATCH, DELIBERATELY, rather than a field query. Every id in this system is
     * already a distinctive string - a character id, a Discord id, a trade id, a dotted
     * action like "trade.completed" - so a plain contains-match answers every question the
     * brief asks with one code path and no query language to learn or to get wrong. The cost
     * is that a short needle matches too much; the caller caps the result and says so.
     *
     * NEWEST FIRST, because an investigation starts from what just happened. The file is
     * append-only so the last matching lines are the recent ones.
     *
     * Not const: the write stream is flushed first, or an entry recorded seconds ago - which
     * is precisely the one being investigated - may still be sitting in the buffer.
     */
    std::vector<std::string> Search(const std::string& acNeedle, size_t aMax);

    const std::string& GetInstanceId() const { return m_instanceId; }

private:
    std::mutex m_mutex;      // handlers run on the game thread today, but the API queue does not
    std::ofstream m_stream;
    std::string m_instanceId;

    // Kept so the ledger can be read back, not just written. Open() had no reason to retain
    // it while this was write-only.
    std::filesystem::path m_path;

    bool m_open{false};
};
