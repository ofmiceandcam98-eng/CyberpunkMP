// Does netpack's enum support actually work?
//
// It had never been asked. No .proto in this project declares an enum, so every TYPE_ENUM
// path in the generator was dead code - and the first one that ran SEGFAULTED the generator
// (exit 139, no output) inside HashProtocol, before emitting anything.
//
// The Stage 6 design proposes two enums, so "does this work" is a prerequisite rather than
// an implementation detail. This proves the whole path end to end: codegen, compile,
// serialize, deserialize, round trip.
//
// Deliberately NOT in tools/tests/. Verify auto-discovers that directory and compiles each
// file with a fixed command; this one needs generated sources that do not exist until
// NetPack has been run. Run it with tools/netpack-scratch/Run.ps1.
//
// When Stage 6 lands a real enum in a real .proto, this should move into Verify proper.

#include "ProtocolPCH.h"
#include "enumtest.gen.h"

#include <cstdio>

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// Serialise one message and read it back through a fresh reader, the way the transport does.
template <class T> static T RoundTrip(const T& acSource)
{
    Buffer buffer(4096);
    Buffer::Writer writer(&buffer);
    acSource.serialize(writer);

    Buffer::Reader reader(&buffer);
    T restored;
    restored.deserialize(reader);
    return restored;
}

int main()
{
    using namespace enumtest;

    // ------------------------------------------------------------ the three values ----
    for (const auto value : {TEST_ZERO, TEST_ONE, TEST_TWO})
    {
        EnumTest msg;
        msg.set_value(value);

        const auto restored = RoundTrip(msg);

        Check(restored.has_value(), "the enum field survives as present");
        Check(restored.get_value() == value, "and round-trips to the same value");
    }

    { // zero is a real value, not "absent"
        //
        // The presence bitfield is what distinguishes them, and an enum whose first member is
        // 0 is exactly where a has_/value confusion would hide. TEST_ZERO set must read back
        // as PRESENT and zero, not as absent.
        EnumTest msg;
        msg.set_value(TEST_ZERO);

        const auto restored = RoundTrip(msg);
        Check(restored.has_value() && restored.get_value() == TEST_ZERO,
              "a zero-valued enum is PRESENT, not mistaken for unset");
    }

    { // never set at all
        EnumTest msg;
        const auto restored = RoundTrip(msg);
        Check(!restored.has_value(), "an unset enum field reads back as absent");
    }

    // ------------------------------------------------ an enum beside other fields ----
    //
    // The shape the Stage 6 proposal actually uses. If the enum mis-sized anything, the
    // fields AFTER it in the presence bitfield are what break - so the string and the
    // repeated field are the real assertions here.
    {
        EnumMixed msg;
        msg.set_verb(TEST_TWO);
        msg.set_revision(42);
        msg.set_note("stage 6");

        Vector<TestEnum> history;
        history.push_back(TEST_ONE);
        history.push_back(TEST_ZERO);
        history.push_back(TEST_TWO);
        msg.set_history(history);

        const auto restored = RoundTrip(msg);

        Check(restored.get_verb() == TEST_TWO, "the enum survives beside other fields");
        Check(restored.get_revision() == 42, "and the uint64 AFTER it is not shifted");
        Check(std::string(restored.get_note().c_str()) == "stage 6",
              "nor the string after that - the presence prefix is sized correctly");
        Check(restored.get_history().size() == 3, "a repeated enum keeps its length");
        Check(restored.get_history()[0] == TEST_ONE &&
              restored.get_history()[1] == TEST_ZERO &&
              restored.get_history()[2] == TEST_TWO,
              "and every element, in order");
    }

    { // a partially-populated message: the enum set, the fields around it not
        EnumMixed msg;
        msg.set_verb(TEST_ONE);

        const auto restored = RoundTrip(msg);

        Check(restored.has_verb() && restored.get_verb() == TEST_ONE, "the set enum arrives");
        Check(!restored.has_revision() && !restored.has_note(),
              "and the unset neighbours stay unset");
    }

    // ------------------------------------------------- THE SECURITY-RELEVANT ONE ----
    //
    // What happens to a value that is not a declared member?
    //
    // This is not academic for Stage 6. `verb` would arrive from an untrusted client, and
    // the question "can a client send a verb the server has never heard of" decides whether
    // the handler needs an explicit whitelist or can trust the type.
    {
        EnumTest msg;
        msg.set_value(static_cast<TestEnum>(9999));

        const auto restored = RoundTrip(msg);

        const bool preserved = static_cast<int>(restored.get_value()) == 9999;

        Check(preserved,
              "AN UNDECLARED ENUM VALUE ROUND-TRIPS INTACT - netpack does NOT validate range");

        // Stated as an assertion so the consequence is recorded next to the evidence:
        // because the wire layer preserves whatever integer arrives, every enum field read
        // from a client is UNTRUSTED INPUT. A handler must switch on known members with an
        // explicit default that refuses - never assume the value is one of the declared ones,
        // and never index an array by it.
        Check(preserved, "=> Stage 6 handlers MUST refuse unknown verbs explicitly");
    }

    { // the generator appends a COUNT sentinel that is not a protocol member
        //
        // Worth pinning: TestEnum_COUNT exists in the generated C++ but is NOT part of
        // kProtocolString, so it does not affect the identifier. A handler must not treat it
        // as a receivable value.
        Check(TestEnum_COUNT == 3, "the generated COUNT sentinel is past the last real member");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
