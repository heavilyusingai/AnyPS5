#include "prx/libSceJson2/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace SceJson2;

static void Require(bool valid, const char* message) {
    if (!valid) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

struct StringTrace {
    std::map<void*, std::size_t> live;
    std::vector<std::size_t> requests;
    int budget = -1;
    int notifications = 0;
};
static void* APS5_VABI AllocateString(void*, std::size_t bytes, void* context) {
    auto& trace = *static_cast<StringTrace*>(context); trace.requests.push_back(bytes);
    if (trace.budget == 0) return nullptr;
    if (trace.budget > 0) --trace.budget;
    void* memory = std::malloc(bytes);
    Require(memory != nullptr, "Tracking allocator requires host memory");
    std::memset(memory, 0xa5, bytes); trace.live.emplace(memory, bytes); return memory;
}
static void APS5_VABI FreeString(void*, void* memory, void* context) {
    if (!memory) return;
    auto& trace = *static_cast<StringTrace*>(context);
    Require(trace.live.erase(memory) == 1, "String must free through its owning allocation context"); std::free(memory);
}
static void APS5_VABI NotifyString(void*, int error, std::size_t bytes, void* context) {
    auto& trace = *static_cast<StringTrace*>(context);
    Require(static_cast<unsigned>(error) == 0x80848102 && trace.requests.back() == bytes, "String failure notification must identify the failed request"); ++trace.notifications;
}
static void* const StringVtable[]{nullptr, nullptr, reinterpret_cast<void*>(AllocateString), reinterpret_cast<void*>(FreeString), reinterpret_cast<void*>(NotifyString)};
struct StringAllocator { void* const* vtable = StringVtable; };
struct Parameter { void* allocator; void* context; std::size_t bufferSize; std::int32_t mode; std::uint32_t reserved[3]; };

static void CheckText(const String& text, const char* bytes, std::size_t length, std::size_t capacity) {
    Require(_ZNK3sce4Json6String4sizeEv(&text) == length && _ZNK3sce4Json6String8capacityEv(&text) == capacity &&
                std::memcmp(_ZNK3sce4Json6String5c_strEv(&text), bytes, length + 1) == 0,
            "String bytes, length and capacity must match the reference");
}

static void PreserveErrno(const char* message) {
    Require(errno == 71, message);
    errno = 71;
}

int main() {
    constexpr auto none = std::numeric_limits<std::size_t>::max();
    auto* const unreadable = reinterpret_cast<String*>(0x10);
    auto* const unreadableText = reinterpret_cast<Text*>(0x10);
    StringTrace trace;
    StringAllocator allocator;
    Parameter parameter{&allocator, &trace, 16384, 2, {}};
    std::uint8_t initializer = 0; _ZN3sce4Json11InitializerC1Ev(&initializer);
    Require(_ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(&initializer, reinterpret_cast<const InitParameter2*>(&parameter)) == 0, "Initialize probe contract");
    const auto initial = trace.live.size();
    String empty{}; _ZN3sce4Json6StringC1Ev(&empty);
    const char* const emptyCharacters = _ZNK3sce4Json6String5c_strEv(&empty);
    const auto baseline = trace.live.size();
    Require(trace.live.at(empty.text) == 24, "The empty string owns only its header");

    errno = 71;

    // An unreadable String object behaves like an empty string for reads and
    // leaves storage untouched for mutations, instead of crashing the host.
    _ZN3sce4Json6StringC1Ev(unreadable);
    _ZN3sce4Json6StringD1Ev(unreadable);
    Require(_ZNK3sce4Json6String5c_strEv(unreadable) == emptyCharacters && _ZNK3sce4Json6String5emptyEv(unreadable) &&
                _ZNK3sce4Json6String6lengthEv(unreadable) == 0 && _ZNK3sce4Json6String4sizeEv(unreadable) == 0 &&
                _ZNK3sce4Json6String8capacityEv(unreadable) == 0 && _ZNK3sce4Json6String2atEm(unreadable, 0) == 0,
            "Unreadable string storage must read as the empty string");
    PreserveErrno("Unreadable string reads must preserve errno");
    _ZN3sce4Json6String5clearEv(unreadable);
    _ZN3sce4Json6String6resizeEm(unreadable, 4);
    _ZN3sce4Json6String7reserveEm(unreadable, 8);
    _ZN3sce4Json6String6appendEPKc(unreadable, "abc");
    _ZN3sce4Json6String6appendERKS1_(unreadable, &empty);
    _ZN3sce4Json6StringaSERKS1_(unreadable, &empty);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable string mutations must not allocate, release or notify");
    PreserveErrno("Unreadable string mutations must preserve errno");
    {
        String result{};
        bool thrown = false;
        try { _ZNK3sce4Json6String6substrEmm(&result, unreadable, 1, 2); }
        catch (const std::out_of_range&) { thrown = true; }
        Require(thrown, "An unreadable string must reject a positive substring position");
        _ZNK3sce4Json6String6substrEmm(&result, unreadable, 0, 2);
        CheckText(result, "", 0, 0);
        _ZN3sce4Json6StringD1Ev(&result);
    }
    Require(_ZNK3sce4Json6String4findEPKcm(unreadable, "a", 0) == none &&
                _ZNK3sce4Json6String5rfindEPKcm(unreadable, "a", none) == none &&
                _ZNK3sce4Json6String4findEcm(unreadable, 'a', 0) == none,
            "Unreadable string searches must return none");
    Require(_ZNK3sce4Json6StringeqEPKc(unreadable, "") && !_ZNK3sce4Json6StringeqEPKc(unreadable, "abc") &&
                _ZNK3sce4Json6StringeqERKS1_(unreadable, &empty),
            "Unreadable string equality must compare as the empty string");
    Require(_ZNK3sce4Json6String7compareEPKc(unreadable, "") == 0 && _ZNK3sce4Json6String7compareEPKc(unreadable, "abc") < 0,
            "Unreadable string comparison must compare as the empty string");
    PreserveErrno("Unreadable string searches and comparisons must preserve errno");

    // An unreadable header behind a readable String object behaves the same.
    String headerless{}; headerless.text = unreadableText;
    _ZN3sce4Json6StringD1Ev(&headerless);
    Require(headerless.text == unreadableText, "An unreadable header must leave the string pointer registered");
    _ZN3sce4Json6String5clearEv(&headerless);
    _ZN3sce4Json6String6resizeEm(&headerless, 2);
    _ZN3sce4Json6String7reserveEm(&headerless, 9);
    _ZN3sce4Json6String6appendEPKc(&headerless, "ab");
    CheckText(headerless, "", 0, 0);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable header operations must not allocate, release or notify");
    PreserveErrno("Unreadable header operations must preserve errno");

    // A readable header with unreadable characters keeps the header contract
    // while character access fails per operation.
    char foreign{}; (void)foreign;
    Text remote{reinterpret_cast<char*>(0x10), 3, 4, UINT64_C(0x123456789abcdef0)};
    String remoteString{&remote};
    Require(_ZNK3sce4Json6String5c_strEv(&remoteString) == reinterpret_cast<const char*>(0x10) &&
                !_ZNK3sce4Json6String5emptyEv(&remoteString) &&
                _ZNK3sce4Json6String6lengthEv(&remoteString) == 3 && _ZNK3sce4Json6String8capacityEv(&remoteString) == 4,
            "Unreadable characters must still honor the readable header");
    Require(_ZNK3sce4Json6String2atEm(&remoteString, 0) == 0 &&
                _ZNK3sce4Json6String4findEPKcm(&remoteString, "abc", 0) == none &&
                _ZNK3sce4Json6String5rfindEcm(&remoteString, 'a', none) == none &&
                !_ZNK3sce4Json6StringeqEPKc(&remoteString, "abc") && _ZNK3sce4Json6StringeqEPKc(&remoteString, ""),
            "Unreadable characters must fail the character-dependent operations");
    Require(_ZNK3sce4Json6String7compareEPKc(&remoteString, "") == 0 && _ZNK3sce4Json6String7compareEPKc(&remoteString, "z") < 0,
            "Unreadable characters must compare as the empty string");
    PreserveErrno("Unreadable character operations must preserve errno");
    _ZN3sce4Json6String6resizeEm(&remoteString, 1);
    Require(remote.length == 3 && remote.capacity == 4, "A failed truncation write-back must leave the header unchanged");
    _ZN3sce4Json6String6resizeEm(&remoteString, 5);
    Require(remote.characters == reinterpret_cast<char*>(0x10) && remote.length == 3 && remote.capacity == 4,
            "A failed growth must roll the new buffer back without touching the header");
    _ZN3sce4Json6String7reserveEm(&remoteString, 64);
    Require(remote.characters == reinterpret_cast<char*>(0x10) && remote.capacity == 4 && trace.live.size() == baseline && trace.notifications == 0,
            "Failed probed growth must release the rolled-back buffer through the allocator without notifying");
    PreserveErrno("Failed probed growth must preserve errno");

    // Unreadable source text throws where null text throws, and degrades where
    // the raw code would have crashed.
    String target{}; _ZN3sce4Json6StringC1EPKc(&target, "abc");
    bool thrown = false;
    try { _ZN3sce4Json6String6appendEPKc(&target, reinterpret_cast<const char*>(0x10)); }
    catch (const std::invalid_argument&) { thrown = true; }
    Require(thrown, "Unreadable append text must throw like null text");
    PreserveErrno("The unreadable append throw must preserve errno");
    thrown = false;
    try { _ZN3sce4Json6String6appendEPKcm(&target, reinterpret_cast<const char*>(0x10), 3); }
    catch (const std::invalid_argument&) { thrown = true; }
    Require(thrown, "Unreadable counted append text must throw like null text");
    _ZN3sce4Json6String6appendEPKcm(&target, reinterpret_cast<const char*>(0x10), 0);
    CheckText(target, "abc", 3, 4);
    thrown = false;
    try { _ZNK3sce4Json6String7compareEPKc(&target, reinterpret_cast<const char*>(0x10)); }
    catch (const std::invalid_argument&) { thrown = true; }
    Require(thrown, "Unreadable comparison text must throw like null text");
    {
        String source{};
        _ZN3sce4Json6StringC1EPKc(&source, reinterpret_cast<const char*>(0x10));
        CheckText(source, "", 0, 0);
        _ZN3sce4Json6StringD1Ev(&source);
    }
    Require(_ZNK3sce4Json6String4findEPKcm(&target, reinterpret_cast<const char*>(0x10), 0) == none &&
                _ZNK3sce4Json6String5rfindEPKcm(&target, reinterpret_cast<const char*>(0x10), none) == none &&
                !_ZNK3sce4Json6StringeqEPKc(&target, reinterpret_cast<const char*>(0x10)),
            "Unreadable needles must return none and compare unequal");
    {
        Text badOther{reinterpret_cast<char*>(0x10), 0, 0, 0};
        String other{&badOther};
        _ZN3sce4Json6String6appendERKS1_(&target, &other);
        CheckText(target, "abc", 3, 4);
        Require(_ZNK3sce4Json6String4findERKS1_m(&target, &other, 0) == none &&
                    _ZNK3sce4Json6String5rfindERKS1_m(&target, &other, none) == none &&
                    !_ZNK3sce4Json6StringeqERKS1_(&target, &other),
                "Unreadable string needles must not append, match or equal");
        thrown = false;
        try { _ZNK3sce4Json6String7compareERKS1_(&target, &other); }
        catch (const std::invalid_argument&) { thrown = true; }
        Require(thrown, "Unreadable comparison other must throw");
        // A zero-length source empties the target through the View-length
        // assignment contract even when its characters are unreadable.
        _ZN3sce4Json6StringaSERKS1_(&target, &other);
        CheckText(target, "", 0, 0);
        PreserveErrno("Unreadable string operand operations must preserve errno");
    }

    // Allocation-failure budgets still behave through the probed paths.
    const auto withTarget = trace.live.size();
    const auto probeNotifications = trace.notifications;
    trace.budget = 0;
    String failed{}; _ZN3sce4Json6StringC1EPKc(&failed, "abc");
    Require(failed.text == nullptr, "A probed header allocation failure must leave the string null");
    CheckText(failed, "", 0, 0);
    _ZN3sce4Json6String7reserveEm(&failed, 8);
    _ZN3sce4Json6String6resizeEm(&failed, 4);
    _ZN3sce4Json6String5clearEv(&failed);
    trace.budget = -1;
    _ZN3sce4Json6StringD1Ev(&failed);
    Require(trace.live.size() == withTarget, "Failed probed construction must stay balanced");

    _ZN3sce4Json6StringD1Ev(&target);
    _ZN3sce4Json6StringD1Ev(&empty);
    Require(trace.live.size() == initial, "Probe contract tests must release all owned resources");
    Require(trace.notifications == probeNotifications + 1, "Exactly the budget failure must notify; probe failures never fire the out-of-memory notifier");
    return 0;
}
