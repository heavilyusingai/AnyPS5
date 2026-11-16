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

struct ValueTrace {
    std::map<void*, std::size_t> live;
    std::vector<std::size_t> requests;
    int budget = -1;
    int notifications = 0;
};
static void* APS5_VABI AllocateValue(void*, std::size_t bytes, void* context) {
    auto& trace = *static_cast<ValueTrace*>(context); trace.requests.push_back(bytes);
    if (trace.budget == 0) return nullptr;
    if (trace.budget > 0) --trace.budget;
    void* memory = std::malloc(bytes);
    Require(memory != nullptr, "Tracking allocator requires host memory");
    std::memset(memory, 0xa5, bytes); trace.live.emplace(memory, bytes); return memory;
}
static void APS5_VABI FreeValue(void*, void* memory, void* context) {
    if (!memory) return;
    auto& trace = *static_cast<ValueTrace*>(context);
    Require(trace.live.erase(memory) == 1, "Value storage must free through its owning allocation context"); std::free(memory);
}
static void APS5_VABI NotifyValue(void*, int error, std::size_t bytes, void* context) {
    auto& trace = *static_cast<ValueTrace*>(context);
    Require(static_cast<unsigned>(error) == 0x80848102 && trace.requests.back() == bytes, "Value failure notification must identify the failed request"); ++trace.notifications;
}
static void* const ValueVtable[]{nullptr, nullptr, reinterpret_cast<void*>(AllocateValue), reinterpret_cast<void*>(FreeValue), reinterpret_cast<void*>(NotifyValue)};
struct ValueAllocator { void* const* vtable = ValueVtable; };
struct Parameter { void* allocator; void* context; std::size_t bufferSize; std::int32_t mode; std::uint32_t reserved[3]; };

static void PreserveErrno(const char* message) {
    Require(errno == 71, message);
    errno = 71;
}
static void CheckBytes(const String& text, const char* bytes, const char* message) {
    Require(std::strcmp(_ZNK3sce4Json6String5c_strEv(&text), bytes) == 0, message);
}
static const Value& APS5_VABI NullRecovery(ValueType, const Value* value, void*) { return *value; }
static const Value& APS5_VABI MismatchRecovery(ValueType, const Value* value, const Value*, void*) { return *value; }
static Value* APS5_VABI FloatRecovery(Value* value, std::int32_t, double, const Value*, void*, bool*) { return value; }
static void APS5_VABI ElementRecovery(int, const String*, const std::size_t*, const Value*, const Value*, void*) {}
static Value CorruptValue(std::uintptr_t payload, ValueType type) {
    Value value{};
    std::memset(&value, 0xa5, sizeof(value));
    value.parent = nullptr;
    value.auxiliary = nullptr;
    value.payload = payload;
    value.type = type;
    return value;
}

int main() {
    auto* const unreadable = reinterpret_cast<Value*>(0x10);
    auto* const foreign = reinterpret_cast<String*>(0x10);
    auto* const foreignStorage = reinterpret_cast<Array*>(0x10);
    ValueTrace trace;
    ValueAllocator allocator;
    Parameter parameter{&allocator, &trace, 16384, 2, {}};
    std::uint8_t initializer = 0; _ZN3sce4Json11InitializerC1Ev(&initializer);
    Require(_ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(&initializer, reinterpret_cast<const InitParameter2*>(&parameter)) == 0, "Initialize the probe contract");
    const auto initial = trace.live.size();
    String text{}; _ZN3sce4Json6StringC1EPKc(&text, "abc");
    const auto baseline = trace.live.size();
    errno = 71;

    // An unreadable Value object survives every entry point without writing,
    // allocating or notifying, and reads fall back to the null-value contract.
    _ZN3sce4Json5ValueC1Ev(unreadable);
    _ZN3sce4Json5ValueC1ENS0_9ValueTypeE(unreadable, ValueType(6));
    _ZN3sce4Json5ValueC1Eb(unreadable, true);
    _ZN3sce4Json5ValueC1El(unreadable, -7);
    _ZN3sce4Json5ValueC1Em(unreadable, 9);
    _ZN3sce4Json5ValueC1Ed(unreadable, 2.5);
    _ZN3sce4Json5ValueC1EPKc(unreadable, "abc");
    _ZN3sce4Json5ValueC1ERKNS0_6StringE(unreadable, &text);
    _ZN3sce4Json5ValueC1ERKS1_(unreadable, unreadable);
    _ZN3sce4Json5ValueD1Ev(unreadable);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable value construction and destruction must not allocate or notify");
    PreserveErrno("Unreadable value construction must preserve errno");
    Require(_ZNK3sce4Json5Value7getTypeEv(unreadable) == TypeNull &&
                *_ZNK3sce4Json5Value10getBooleanEv(unreadable) == false &&
                *_ZNK3sce4Json5Value10getIntegerEv(unreadable) == 0 &&
                *_ZNK3sce4Json5Value11getUIntegerEv(unreadable) == 0 &&
                *_ZNK3sce4Json5Value7getRealEv(unreadable) == 0.0 &&
                _ZNK3sce4Json5Value9getStringEv(unreadable) != nullptr &&
                _ZNK3sce4Json5Value8getArrayEv(unreadable) != nullptr &&
                _ZNK3sce4Json5Value9getObjectEv(unreadable) != nullptr &&
                _ZNK3sce4Json5Value5countEv(unreadable) == 0 &&
                !_ZNK3sce4Json5ValuecvbEv(unreadable),
            "Unreadable value reads must present the null-value contract");
    PreserveErrno("Unreadable value reads must preserve errno");
    Require(_ZNK3sce4Json5ValueixERKNS0_6StringE(unreadable, &text) == nullptr &&
                _ZNK3sce4Json5ValueixEPKc(unreadable, "abc") == nullptr &&
                _ZNK3sce4Json5ValueixEPKc(unreadable, nullptr) == nullptr &&
                _ZNK3sce4Json5ValueixEm(unreadable, 0) != nullptr &&
                _ZNK3sce4Json5ValueixEm(unreadable, 0) != unreadable &&
                _ZNK3sce4Json5Value8getValueERKNS0_6StringE(unreadable, &text) == nullptr &&
                _ZN3sce4Json5Value10referValueEm(unreadable, 0) == nullptr &&
                _ZN3sce4Json5Value10referValueERKNS0_6StringE(unreadable, &text) == nullptr &&
                _ZN3sce4Json5Value11referStringEv(unreadable) == nullptr &&
                _ZN3sce4Json5Value12referBooleanEv(unreadable) == nullptr &&
                _ZN3sce4Json5Value12referIntegerEv(unreadable) == nullptr &&
                _ZN3sce4Json5Value13referUIntegerEv(unreadable) == nullptr &&
                _ZN3sce4Json5Value9referRealEv(unreadable) == nullptr,
            "Unreadable value element access must miss without dereferencing");
    PreserveErrno("Unreadable element access must preserve errno");
    {
        bool thrown = false;
        try { _ZN3sce4Json5Value10referArrayEv(unreadable); }
        catch (const std::runtime_error&) { thrown = true; }
        Require(thrown, "An unreadable value must reject referArray");
        thrown = false;
        try { _ZN3sce4Json5Value11referObjectEv(unreadable); }
        catch (const std::runtime_error&) { thrown = true; }
        Require(thrown, "An unreadable value must reject referObject");
    }
    _ZN3sce4Json5Value3setENS0_9ValueTypeE(unreadable, ValueType(2));
    _ZN3sce4Json5Value3setEb(unreadable, true);
    _ZN3sce4Json5Value3setEl(unreadable, 5);
    _ZN3sce4Json5Value3setEm(unreadable, 6);
    _ZN3sce4Json5Value3setEd(unreadable, 1.5);
    _ZN3sce4Json5Value3setEPKc(unreadable, "abc");
    _ZN3sce4Json5Value3setERKNS0_6StringE(unreadable, &text);
    _ZN3sce4Json5Value3setERKS1_(unreadable, unreadable);
    _ZN3sce4Json5Value5clearEv(unreadable);
    _ZN3sce4Json5Value4swapERS1_(unreadable, unreadable);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable value mutations must not allocate, release or notify");
    PreserveErrno("Unreadable value mutations must preserve errno");
    {
        String out{}; _ZN3sce4Json6StringC1Ev(&out);
        _ZNK3sce4Json5Value8toStringERNS0_6StringE(unreadable, &out);
        CheckBytes(out, "", "Unreadable toString must leave the output untouched");
        _ZN3sce4Json6StringD1Ev(&out);
    }
    Require(_ZN3sce4Json5Value21setNullAccessCallBackEPFRKS1_NS0_9ValueTypeEPS2_PvES6_(unreadable, NullRecovery, &trace) == static_cast<int>(0x80848104) &&
                _ZN3sce4Json5Value22setTypeMismatchHandlerEPFRKS1_NS0_9ValueTypeEPS2_S5_PvES6_(unreadable, MismatchRecovery, &trace) == static_cast<int>(0x80848104) &&
                _ZN3sce4Json5Value22setSpecialFloatHandlerEPFKS1_NS0_12FunctionTypeEdPS2_PvPbES5_(unreadable, FloatRecovery, &trace) == static_cast<int>(0x80848104) &&
                _ZN3sce4Json5Value30setElementAccessFailureHandlerEPFviPKNS0_6StringEPKmPKS1_S8_PvES9_(unreadable, ElementRecovery, &trace) == static_cast<int>(0x80848104),
            "Unreadable value handler registration must report the parent error");
    PreserveErrno("Unreadable handler registration must preserve errno");

    // A readable value whose string handle is unreadable degrades every
    // payload-dependent operation instead of crashing the host.
    {
        Value value = CorruptValue(reinterpret_cast<std::uintptr_t>(foreign), TypeString);
        Require(_ZNK3sce4Json5Value9getStringEv(&value) == foreign,
                "A readable value must still report its registered payload handle");
        Require(!_ZNK3sce4Json5ValuecvbEv(&value), "An unreadable string payload must read as empty");
        _ZNK3sce4Json5Value8toStringERNS0_6StringE(&value, &text);
        CheckBytes(text, "abc", "Unreadable payload toString must leave the output untouched");
        _ZN3sce4Json5Value5clearEv(&value);
        Require(value.type == TypeNull && value.string == nullptr && value.reserved == 0xa5a5a5a5 && trace.live.size() == baseline,
                "Clearing an unreadable payload must reset the value without touching foreign storage");
        PreserveErrno("Unreadable payload operations must preserve errno");
    }
    {
        Value value = CorruptValue(reinterpret_cast<std::uintptr_t>(foreign), TypeString);
        Value source{}; _ZN3sce4Json5ValueC1EPKc(&source, "def");
        errno = 71;
        _ZN3sce4Json5Value4swapERS1_(&value, &source);
        Require(_ZNK3sce4Json5Value9getStringEv(&value) == foreign && value.type == TypeString,
                "Swapping over an unreadable payload must leave it registered");
        Require(source.type == TypeNull, "The swapped-out unreadable payload must null its partner");
        _ZN3sce4Json5Value4swapERS1_(&value, &source);
        Require(trace.live.size() == baseline, "Swapping an unreadable payload must not allocate");
        errno = 71;
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "Destroying an unreadable payload must not release foreign storage");
        _ZN3sce4Json5ValueD1Ev(&source);
        Require(trace.live.size() == baseline, "The swap partner must keep its own storage balanced");
        PreserveErrno("Unreadable payload swaps must preserve errno");
    }

    // An array handle that points at unreadable storage reads as empty and
    // misses every element access.
    {
        Value value = CorruptValue(reinterpret_cast<std::uintptr_t>(foreignStorage), TypeArray);
        Require(_ZNK3sce4Json5Value5countEv(&value) == 0 &&
                    _ZNK3sce4Json5ValueixEm(&value, 0) != nullptr &&
                    _ZNK3sce4Json5ValueixEm(&value, 0) != &value &&
                    _ZN3sce4Json5Value10referValueEm(&value, 0) == nullptr &&
                    _ZN3sce4Json5Value10referArrayEv(&value) == foreignStorage,
            "Unreadable array storage must count zero and miss element access");
        _ZN3sce4Json6String5clearEv(&text);
        _ZNK3sce4Json5Value8toStringERNS0_6StringE(&value, &text);
        CheckBytes(text, "array", "Array toString must label the value");
        errno = 71;
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "Destroying unreadable array storage must stay balanced");
        PreserveErrno("Unreadable array storage must preserve errno");
    }

    // Readable round-trips keep the exact value semantics through the probed
    // layer: payloads, references, swapping and type conversion.
    {
        Value value{}; _ZN3sce4Json5ValueC1Ev(&value);
        _ZN3sce4Json5Value3setEPKc(&value, "hello");
        Require(value.type == TypeString && std::strcmp(_ZNK3sce4Json6String5c_strEv(_ZNK3sce4Json5Value9getStringEv(&value)), "hello") == 0,
                "Probed string setting must store readable characters");
        _ZN3sce4Json5Value3setEl(&value, -42);
        Require(value.type == TypeInteger && *_ZNK3sce4Json5Value10getIntegerEv(&value) == -42 &&
                    *_ZNK3sce4Json5Value11getUIntegerEv(&value) == UINT64_C(18446744073709551574),
                "Probed integer reads must cross-access the unsigned view");
        const auto* integerStorage = _ZNK3sce4Json5Value10getIntegerEv(&value);
        _ZN3sce4Json5Value3setEm(&value, 42);
        Require(integerStorage == _ZNK3sce4Json5Value10getIntegerEv(&value) && *integerStorage == 42,
                "Probed integer storage must stay bound to the value payload");
        _ZN3sce4Json5Value3setEd(&value, 1.25);
        Require(value.type == TypeReal && *_ZNK3sce4Json5Value7getRealEv(&value) == 1.25, "Probed real setting must store the value");
        _ZN3sce4Json5Value3setEb(&value, true);
        Require(value.type == TypeBoolean && *_ZNK3sce4Json5Value10getBooleanEv(&value) && _ZNK3sce4Json5ValuecvbEv(&value),
                "Probed boolean setting must store the value");
        _ZN3sce4Json6String5clearEv(&text);
        _ZNK3sce4Json5Value8toStringERNS0_6StringE(&value, &text);
        CheckBytes(text, "true", "Boolean toString must serialize the value");
        _ZN3sce4Json5Value3setENS0_9ValueTypeE(&value, ValueType(0));
        Require(value.type == TypeNull && !_ZNK3sce4Json5ValuecvbEv(&value) && _ZNK3sce4Json5Value5countEv(&value) == 0,
                "Probed type setting must reset to null");
        {
            Array* array = _ZN3sce4Json5Value10referArrayEv(&value);
            Require(array != nullptr && value.type == TypeArray, "referArray on null must create the array");
            Value item{}; _ZN3sce4Json5ValueC1El(&item, 7);
            _ZN3sce4Json5Array9push_backERKNS0_5ValueE(array, &item);
            _ZN3sce4Json5ValueD1Ev(&item);
            Require(_ZNK3sce4Json5Value5countEv(&value) == 1 && *_ZNK3sce4Json5Value10getIntegerEv(_ZNK3sce4Json5ValueixEm(&value, 0)) == 7,
                    "Probed arrays must index their probed children");
            _ZN3sce4Json6String5clearEv(&text);
            _ZNK3sce4Json5Value8toStringERNS0_6StringE(&value, &text);
            CheckBytes(text, "array", "Array toString must label the value");
            Value* stored = _ZN3sce4Json5Value10referValueEm(&value, 0);
            Require(stored != nullptr && stored->parent == &value, "Probed array children must bind to their owner");
            Require(_ZN3sce4Json5Value21setNullAccessCallBackEPFRKS1_NS0_9ValueTypeEPS2_PvES6_(stored, NullRecovery, &trace) == static_cast<int>(0x80848104),
                    "A child value must reject handler registration like the raw contract");
        }
        {
            const Value* missing = _ZNK3sce4Json5ValueixEPKc(&value, "missing");
            Require(missing != nullptr && missing != &value, "A missing object key must resolve to the fallback value");
            Require(_ZN3sce4Json5Value10referValueEm(const_cast<Value*>(missing), 0) == nullptr,
                    "The missing-element fallback must expose no elements");
        }
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "Probed round-trips must release all owned resources");
    }
    {
        Value value{}; _ZN3sce4Json5ValueC1Ev(&value);
        Object* object = _ZN3sce4Json5Value11referObjectEv(&value);
        Require(object != nullptr && value.type == TypeObject, "referObject on null must create the object");
        _ZN3sce4Json5ValueD1Ev(&value);
        value = Value{};
        _ZN3sce4Json5ValueC1El(&value, 5);
        bool thrown = false;
        try { _ZN3sce4Json5Value11referObjectEv(&value); }
        catch (const std::runtime_error&) { thrown = true; }
        Require(thrown, "referObject on a scalar must throw");
        thrown = false;
        try { _ZN3sce4Json5Value3setEPKc(&value, nullptr); }
        catch (const std::invalid_argument&) { thrown = true; }
        Require(thrown, "Null set text must throw like the raw contract");
        _ZN3sce4Json5Value3setEd(&value, std::numeric_limits<double>::infinity());
        Require(_ZNK3sce4Json5Value7getRealEv(&value) == &value.real, "Special float setting must store through the value payload");
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "The conversion block must release all owned resources");
    }
    {
        Value left{}; Value right{};
        _ZN3sce4Json5ValueC1EPKc(&left, "first");
        _ZN3sce4Json5ValueC1El(&right, 11);
        _ZN3sce4Json5Value4swapERS1_(&left, &right);
        Require(*_ZNK3sce4Json5Value10getIntegerEv(&left) == 11 &&
                    std::strcmp(_ZNK3sce4Json6String5c_strEv(_ZNK3sce4Json5Value9getStringEv(&right)), "first") == 0,
                "Probed swapping must exchange payloads");
        _ZN3sce4Json5Value4swapERS1_(&left, &left);
        Require(*_ZNK3sce4Json5Value10getIntegerEv(&left) == 11, "Probed self-swap must retain the payload");
        _ZN3sce4Json5ValueaSERKS1_(&left, &right);
        Require(std::strcmp(_ZNK3sce4Json6String5c_strEv(_ZNK3sce4Json5Value9getStringEv(&left)), "first") == 0,
                "Probed assignment must copy the source payload");
        _ZN3sce4Json5ValueD1Ev(&left);
        _ZN3sce4Json5ValueD1Ev(&right);
    }
    {
        Value root{}; _ZN3sce4Json5ValueC1Ev(&root);
        Require(_ZN3sce4Json5Value21setNullAccessCallBackEPFRKS1_NS0_9ValueTypeEPS2_PvES6_(&root, NullRecovery, &trace) == 0 &&
                    _ZN3sce4Json5Value22setTypeMismatchHandlerEPFRKS1_NS0_9ValueTypeEPS2_S5_PvES6_(&root, MismatchRecovery, &trace) == 0 &&
                    _ZN3sce4Json5Value22setSpecialFloatHandlerEPFKS1_NS0_12FunctionTypeEdPS2_PvPbES5_(&root, FloatRecovery, &trace) == 0 &&
                    _ZN3sce4Json5Value30setElementAccessFailureHandlerEPFviPKNS0_6StringEPKmPKS1_S8_PvES9_(&root, ElementRecovery, &trace) == 0,
                "Root handler registration must succeed on a probed root value");
        _ZN3sce4Json5ValueD1Ev(&root);
        Require(trace.live.size() == baseline, "Handler registration must release its records on destruction");
    }

    // Allocation-failure budgets keep the partial-state and teardown balance.
    for (int budget = 0; budget <= 2; ++budget) {
        Value value; std::memset(&value, 0xa5, sizeof(value));
        trace.budget = budget;
        _ZN3sce4Json5ValueC1EPKc(&value, "abc");
        trace.budget = -1;
        Require(value.reserved == 0xa5a5a5a5, "Probed construction must preserve inactive metadata bytes");
        errno = 71;
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "Failed probed construction must stay balanced");
        PreserveErrno("Failed probed construction must preserve errno");
    }
    {
        Value value; std::memset(&value, 0xa5, sizeof(value));
        trace.budget = 1;
        _ZN3sce4Json5ValueC1ENS0_9ValueTypeE(&value, ValueType(6));
        trace.budget = -1;
        Require(value.type == TypeNull && value.payload == 0, "A failed array handle allocation must clear the payload");
        errno = 71;
        _ZN3sce4Json5ValueD1Ev(&value);
        Require(trace.live.size() == baseline, "Failed typed construction must stay balanced");
    }

    _ZN3sce4Json6StringD1Ev(&text);
    Require(trace.live.size() == initial, "Probe contract tests must release all owned resources");
    return 0;
}
