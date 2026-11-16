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
#include <string>
#include <vector>

using namespace SceJson2;

static void Require(bool valid, const char* message) {
    if (!valid) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

struct CollectionTrace {
    std::map<void*, std::size_t> live;
    std::vector<std::size_t> requests;
    int budget = -1;
    int notifications = 0;
};
static void* APS5_VABI AllocateCollection(void*, std::size_t bytes, void* context) {
    auto& trace = *static_cast<CollectionTrace*>(context); trace.requests.push_back(bytes);
    if (trace.budget == 0) return nullptr;
    if (trace.budget > 0) --trace.budget;
    void* memory = std::malloc(bytes);
    Require(memory != nullptr, "Tracking allocator requires host memory");
    std::memset(memory, 0xa5, bytes); trace.live.emplace(memory, bytes); return memory;
}
static void APS5_VABI FreeCollection(void*, void* memory, void* context) {
    if (!memory) return;
    auto& trace = *static_cast<CollectionTrace*>(context);
    trace.live.erase(memory); std::free(memory);
}
static void APS5_VABI NotifyCollection(void*, int error, std::size_t bytes, void* context) {
    auto& trace = *static_cast<CollectionTrace*>(context);
    Require(static_cast<unsigned>(error) == 0x80848102 && trace.requests.back() == bytes, "Collection failure notification must identify the failed request"); ++trace.notifications;
}
static void* const CollectionVtable[]{nullptr, nullptr, reinterpret_cast<void*>(AllocateCollection), reinterpret_cast<void*>(FreeCollection), reinterpret_cast<void*>(NotifyCollection)};
struct CollectionAllocator { void* const* vtable = CollectionVtable; };
struct Parameter { void* allocator; void* context; std::size_t bufferSize; std::int32_t mode; std::uint32_t reserved[3]; };

static void PreserveErrno(const char* message) {
    Require(errno == 71, message);
    errno = 71;
}
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
    auto* const unreadable = reinterpret_cast<Array*>(0x10);
    auto* const unreadableObject = reinterpret_cast<Object*>(0x10);
    auto* const unreadableIterator = reinterpret_cast<ArrayIterator*>(0x10);
    auto* const unreadableObjectIterator = reinterpret_cast<ObjectIterator*>(0x10);
    auto* const unreadablePair = reinterpret_cast<Pair*>(0x10);
    CollectionTrace trace;
    CollectionAllocator allocator;
    Parameter parameter{&allocator, &trace, 16384, 2, {}};
    std::uint8_t initializer = 0; _ZN3sce4Json11InitializerC1Ev(&initializer);
    Require(_ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(&initializer, reinterpret_cast<const InitParameter2*>(&parameter)) == 0, "Initialize the probe contract");
    const auto initial = trace.live.size();
    const auto baseline = trace.live.size();
    errno = 71;

    // An unreadable Array object survives every entry point without writing,
    // allocating or notifying, and reads fall back to the empty contract.
    _ZN3sce4Json5ArrayC1Ev(unreadable);
    _ZN3sce4Json5ArrayC1ERKS1_(unreadable, unreadable);
    _ZN3sce4Json5ArrayD1Ev(unreadable);
    _ZN3sce4Json5ArrayaSERKS1_(unreadable, unreadable);
    _ZN3sce4Json5Array9push_backERKNS0_5ValueE(unreadable, nullptr);
    _ZN3sce4Json5Array10push_frontERKNS0_5ValueE(unreadable, nullptr);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable array lifetime and insertion must not allocate or notify");
    PreserveErrno("Unreadable array lifetime must preserve errno");
    {
        ArrayIterator result{};
        Value element{};
        _ZN3sce4Json5Array6insertERKNS1_8iteratorERKNS0_5ValueE(&result, unreadable, nullptr, &element);
        Require(result.state == nullptr, "Insertion into an unreadable array must not publish an iterator state");
    }
    Require(_ZNK3sce4Json5Array5emptyEv(unreadable) && _ZNK3sce4Json5Array4sizeEv(unreadable) == 0 &&
                _ZNK3sce4Json5Array5frontEv(unreadable) != nullptr && reinterpret_cast<const void*>(_ZNK3sce4Json5Array5frontEv(unreadable)) != reinterpret_cast<const void*>(unreadable) &&
                _ZNK3sce4Json5Array4backEv(unreadable) != nullptr && reinterpret_cast<const void*>(_ZNK3sce4Json5Array4backEv(unreadable)) != reinterpret_cast<const void*>(unreadable),
            "Unreadable array reads must present the empty contract");
    PreserveErrno("Unreadable array reads must preserve errno");
    {
        ArrayIterator result{};
        _ZNK3sce4Json5Array5beginEv(&result, unreadable);
        Require(result.state == nullptr, "Begin on an unreadable array must not publish an iterator state");
        _ZNK3sce4Json5Array3endEv(&result, unreadable);
        Require(result.state == nullptr, "End on an unreadable array must not publish an iterator state");
    }
    _ZN3sce4Json5Array5clearEv(unreadable);
    _ZN3sce4Json5Array8pop_backEv(unreadable);
    _ZN3sce4Json5Array9pop_frontEv(unreadable);
    {
        ArrayIterator result{};
        _ZN3sce4Json5Array5eraseERKNS1_8iteratorE(&result, unreadable, nullptr);
        Require(result.state == nullptr, "Erase on an unreadable array must not publish an iterator state");
    }
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable array mutations must not allocate, release or notify");
    PreserveErrno("Unreadable array mutations must preserve errno");

    // Unreadable array iterators survive every operation.
    _ZN3sce4Json5Array8iteratorD1Ev(unreadableIterator);
    Require(_ZN3sce4Json5Array8iteratorppEv(unreadableIterator) == unreadableIterator, "Increment on an unreadable iterator must return its receiver");
    Require(_ZNK3sce4Json5Array8iteratordeEv(unreadableIterator) != nullptr && reinterpret_cast<const void*>(_ZNK3sce4Json5Array8iteratordeEv(unreadableIterator)) != reinterpret_cast<const void*>(unreadableIterator),
            "Dereference on an unreadable iterator must return the null-value default");
    Require(!_ZNK3sce4Json5Array8iteratorneERKS2_(unreadableIterator, unreadableIterator), "Comparison against an unreadable iterator must compare equal");
    {
        ArrayIterator result{};
        _ZN3sce4Json5Array8iteratorC1ERKS2_(&result, unreadableIterator);
        Require(result.state == nullptr, "Copying an unreadable iterator must leave the copy unbound");
    }
    _ZN3sce4Json5Array8iterator7advanceEm(unreadableIterator, 3);
    _ZN3sce4Json5Array8iteratoraSERKS2_(unreadableIterator, unreadableIterator);
    _ZN3sce4Json5Array8iteratorC1Ev(unreadableIterator);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable iterator operations must not allocate, release or notify");
    PreserveErrno("Unreadable iterator operations must preserve errno");

    // The same contract holds for Object and its iterators.
    _ZN3sce4Json6ObjectC1Ev(unreadableObject);
    _ZN3sce4Json6ObjectC1ERKS1_(unreadableObject, unreadableObject);
    _ZN3sce4Json6ObjectD1Ev(unreadableObject);
    _ZN3sce4Json6ObjectaSERKS1_(unreadableObject, unreadableObject);
    Require(_ZNK3sce4Json6Object5emptyEv(unreadableObject) && _ZNK3sce4Json6Object4sizeEv(unreadableObject) == 0,
            "Unreadable object reads must present the empty contract");
    {
        ObjectIterator result{};
        _ZNK3sce4Json6Object5beginEv(&result, unreadableObject);
        Require(result.state == nullptr, "Begin on an unreadable object must not publish an iterator state");
        _ZNK3sce4Json6Object3endEv(&result, unreadableObject);
        Require(result.state == nullptr, "End on an unreadable object must not publish an iterator state");
    }
    _ZN3sce4Json6Object5clearEv(unreadableObject);
    Require(_ZN3sce4Json6ObjectixERKNS0_6StringE(unreadableObject, nullptr) != nullptr && reinterpret_cast<void*>(_ZN3sce4Json6ObjectixERKNS0_6StringE(unreadableObject, nullptr)) != reinterpret_cast<void*>(unreadableObject),
            "Indexing an unreadable object must return the null-value default");
    {
        ObjectIterator result{};
        _ZNK3sce4Json6Object4findERKNS0_6StringE(&result, unreadableObject, nullptr);
        Require(result.state == nullptr, "Find on an unreadable object must not publish an iterator state");
    }
    _ZN3sce4Json6Object5eraseERKNS0_6StringE(unreadableObject, nullptr);
    {
        Pair pairElement{};
        ObjectIterator result{};
        _ZN3sce4Json6Object6insertERKNS1_4PairE(&result, unreadableObject, &pairElement);
        Require(result.state == nullptr, "Insertion into an unreadable object must not publish an iterator state");
    }
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable object operations must not allocate, release or notify");
    PreserveErrno("Unreadable object operations must preserve errno");
    _ZN3sce4Json6Object8iteratorD1Ev(unreadableObjectIterator);
    Require(_ZN3sce4Json6Object8iteratorppEv(unreadableObjectIterator) == unreadableObjectIterator, "Object increment on an unreadable iterator must return its receiver");
    Require(_ZNK3sce4Json6Object8iteratordeEv(unreadableObjectIterator) != nullptr && reinterpret_cast<const void*>(_ZNK3sce4Json6Object8iteratordeEv(unreadableObjectIterator)) != reinterpret_cast<const void*>(unreadableObjectIterator),
            "Object dereference on an unreadable iterator must return a pair from the defaults");
    Require(!_ZNK3sce4Json6Object8iteratoreqERKS2_(unreadableObjectIterator, unreadableObjectIterator) &&
                !_ZNK3sce4Json6Object8iteratorneERKS2_(unreadableObjectIterator, unreadableObjectIterator),
            "Object comparisons against an unreadable iterator must report neither equality nor inequality");
    {
        ObjectIterator result{};
        _ZN3sce4Json6Object8iteratorC1ERKS2_(&result, unreadableObjectIterator);
        Require(result.state == nullptr, "Copying an unreadable object iterator must leave the copy unbound");
    }
    _ZN3sce4Json6Object8iterator7advanceEm(unreadableObjectIterator, 2);
    _ZN3sce4Json6Object8iteratoraSERKS2_(unreadableObjectIterator, unreadableObjectIterator);
    _ZN3sce4Json6Object8iteratorC1Ev(unreadableObjectIterator);
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable object iterator operations must not allocate, release or notify");
    PreserveErrno("Unreadable object iterator operations must preserve errno");

    // An unreadable Pair survives construction and destruction.
    _ZN3sce4Json6Object4PairC1Ev(unreadablePair);
    {
        String key{}; _ZN3sce4Json6StringC1EPKc(&key, "k");
        Value value{}; _ZN3sce4Json5ValueC1El(&value, 5);
        _ZN3sce4Json6Object4PairC1ERKNS0_6StringERKNS0_5ValueE(unreadablePair, &key, &value);
        _ZN3sce4Json6Object4PairD1Ev(unreadablePair);
        _ZN3sce4Json5ValueD1Ev(&value);
        _ZN3sce4Json6StringD1Ev(&key);
    }
    Require(trace.live.size() == baseline && trace.notifications == 0,
            "Unreadable pair lifetime must not allocate, release or notify");
    PreserveErrno("Unreadable pair lifetime must preserve errno");

    // Readable collections with unreadable payloads and keys degrade per
    // operation instead of crashing the host.
    {
        Array array{}; _ZN3sce4Json5ArrayC1Ev(&array);
        Value corrupt = CorruptValue(static_cast<std::uintptr_t>(0x10), TypeString);
        _ZN3sce4Json5Array9push_backERKNS0_5ValueE(&array, &corrupt);
        Require(_ZNK3sce4Json5Array4sizeEv(&array) == 1, "Pushing an unreadable value must still insert a slot");
        const auto* element = _ZNK3sce4Json5Array5frontEv(&array);
        Require(element && element->type == TypeNull, "The inserted slot must hold a null value");
        ArrayIterator selected{}, result{};
        _ZNK3sce4Json5Array5beginEv(&selected, &array);
        _ZN3sce4Json5Array5eraseERKNS1_8iteratorE(&result, &array, &selected);
        Require(_ZNK3sce4Json5Array4sizeEv(&array) == 0, "Erasing the inserted slot must restore the empty array");
        _ZN3sce4Json5Array8iteratorD1Ev(&result);
        _ZN3sce4Json5Array8iteratorD1Ev(&selected);
        _ZN3sce4Json5ArrayD1Ev(&array);
        Require(trace.live.size() == baseline, "The corrupt-value array block must release all owned resources");
        PreserveErrno("Corrupt-value array operations must preserve errno");
    }
    {
        Object object{}; _ZN3sce4Json6ObjectC1Ev(&object);
        Text foreign{reinterpret_cast<char*>(0x10), 2, 3, 0};
        String key{&foreign};
        auto* created = _ZN3sce4Json6ObjectixERKNS0_6StringE(&object, &key);
        Require(created != nullptr && created->type == TypeNull, "Indexing with an unreadable key must return the null default without creating an entry");
        Require(_ZNK3sce4Json6Object4sizeEv(&object) == 0, "An unreadable key must not insert an entry");
        {
            ObjectIterator found{};
            _ZNK3sce4Json6Object4findERKNS0_6StringE(&found, &object, &key);
            Require(found.state != nullptr && *reinterpret_cast<void**>(found.state) != nullptr, "Finding an unreadable key on a fresh object must bind to the not-found sentinel like raw find");
            _ZN3sce4Json6Object8iteratorD1Ev(&found);
        }
        _ZN3sce4Json6Object5eraseERKNS0_6StringE(&object, &key);
        Require(_ZNK3sce4Json6Object4sizeEv(&object) == 0, "Erasing an unreadable key must do nothing");
        {
            Pair pairElement{};
            _ZN3sce4Json5ValueC1Ev(&pairElement.value);
            pairElement.key.text = reinterpret_cast<Text*>(0x10);
            ObjectIterator result{};
            _ZN3sce4Json6Object6insertERKNS1_4PairE(&result, &object, &pairElement);
            Require(result.state != nullptr && *reinterpret_cast<void**>(result.state) == nullptr, "Inserting a pair with an unreadable key must leave the iterator unbound");
            Require(_ZNK3sce4Json6Object4sizeEv(&object) == 0, "Inserting a pair with an unreadable key must not create an entry");
            _ZN3sce4Json6Object8iteratorD1Ev(&result);
            _ZN3sce4Json6Object4PairD1Ev(&pairElement);
        }
        _ZN3sce4Json6ObjectD1Ev(&object);
        Require(trace.live.size() == baseline, "The unreadable-key object block must release all owned resources");
        PreserveErrno("Unreadable-key object operations must preserve errno");
    }

    // Readable round-trips keep the exact collection semantics through the
    // probed layer: insertion, iteration, erasure and teardown.
    {
        Array array{}; _ZN3sce4Json5ArrayC1Ev(&array);
        for (int number : {10, 20, 30}) {
            Value value{}; _ZN3sce4Json5ValueC1El(&value, number);
            _ZN3sce4Json5Array9push_backERKNS0_5ValueE(&array, &value);
            _ZN3sce4Json5ValueD1Ev(&value);
        }
        Require(_ZNK3sce4Json5Array4sizeEv(&array) == 3 && *_ZNK3sce4Json5Value10getIntegerEv(_ZNK3sce4Json5Array5frontEv(&array)) == 10 &&
                    *_ZNK3sce4Json5Value10getIntegerEv(_ZNK3sce4Json5Array4backEv(&array)) == 30,
                "Probed arrays must store pushed values in order");
        ArrayIterator selected{}, result{}, end{};
        _ZNK3sce4Json5Array5beginEv(&selected, &array);
        _ZN3sce4Json5Array8iterator7advanceEm(&selected, 1);
        _ZNK3sce4Json5Array3endEv(&end, &array);
        Require(_ZNK3sce4Json5Array8iteratorneERKS2_(&selected, &end), "The advanced iterator must differ from end");
        Require(*_ZNK3sce4Json5Value10getIntegerEv(_ZNK3sce4Json5Array8iteratordeEv(&selected)) == 20,
                "The advanced iterator must reference the second element");
        _ZN3sce4Json5Array5eraseERKNS1_8iteratorE(&result, &array, &selected);
        Require(_ZNK3sce4Json5Array4sizeEv(&array) == 2 && *_ZNK3sce4Json5Value10getIntegerEv(_ZNK3sce4Json5Array8iteratordeEv(&result)) == 30,
                "Erasing must return the successor iterator");
        _ZN3sce4Json5Array8iteratorD1Ev(&result);
        _ZN3sce4Json5Array8iteratorD1Ev(&selected);
        _ZN3sce4Json5Array8iteratorD1Ev(&end);
        _ZN3sce4Json5Array8pop_backEv(&array);
        _ZN3sce4Json5Array9pop_frontEv(&array);
        Require(_ZNK3sce4Json5Array4sizeEv(&array) == 0, "Popping must empty the array");
        _ZN3sce4Json5ArrayD1Ev(&array);
        Require(trace.live.size() == baseline, "The array round-trip must release all owned resources");
    }
    {
        Object object{}; _ZN3sce4Json6ObjectC1Ev(&object);
        String key{}; _ZN3sce4Json6StringC1EPKc(&key, "entry");
        auto* slot = _ZN3sce4Json6ObjectixERKNS0_6StringE(&object, &key);
        Require(slot != nullptr && slot->type == TypeNull, "Indexing must create a null entry");
        _ZN3sce4Json5Value3setEl(slot, 44);
        Require(_ZNK3sce4Json6Object4sizeEv(&object) == 1, "The created entry must count");
        {
            ObjectIterator found{};
            _ZNK3sce4Json6Object4findERKNS0_6StringE(&found, &object, &key);
            Require(found.state != nullptr && *reinterpret_cast<void**>(found.state) != nullptr, "Finding the stored key must bind the iterator");
            auto* pair = _ZNK3sce4Json6Object8iteratordeEv(&found);
            Require(pair && pair->value.type == TypeInteger && pair->value.payload == 44, "The found pair must reference the stored entry");
            _ZN3sce4Json6Object8iteratorD1Ev(&found);
        }
        {
            Pair pairElement{};
            _ZN3sce4Json6StringC1EPKc(&pairElement.key, "other");
            _ZN3sce4Json5ValueC1El(&pairElement.value, 8);
            ObjectIterator result{};
            _ZN3sce4Json6Object6insertERKNS1_4PairE(&result, &object, &pairElement);
            Require(result.state != nullptr && *reinterpret_cast<void**>(result.state) != nullptr, "Inserting an independent pair must bind its iterator");
            Require(_ZNK3sce4Json6Object4sizeEv(&object) == 2, "The inserted pair must count");
            _ZN3sce4Json6Object8iteratorD1Ev(&result);
            _ZN3sce4Json6Object4PairD1Ev(&pairElement);
        }
        _ZN3sce4Json6Object5eraseERKNS0_6StringE(&object, &key);
        Require(_ZNK3sce4Json6Object4sizeEv(&object) == 1, "Erasing the stored key must remove its entry");
        _ZN3sce4Json6ObjectD1Ev(&object);
        _ZN3sce4Json6StringD1Ev(&key);
        Require(trace.live.size() == baseline, "The object round-trip must release all owned resources");
    }
    {
        Pair pair{};
        _ZN3sce4Json6Object4PairC1Ev(&pair);
        Require(pair.key.text != nullptr && pair.value.type == TypeNull, "Default pair construction must own an empty key and a null value");
        _ZN3sce4Json6Object4PairD1Ev(&pair);
        Require(trace.live.size() == baseline, "Default pair construction must release cleanly");
        String key{}; _ZN3sce4Json6StringC1EPKc(&key, "named");
        Value value{}; _ZN3sce4Json5ValueC1El(&value, 7);
        Pair copied{};
        _ZN3sce4Json6Object4PairC1ERKNS0_6StringERKNS0_5ValueE(&copied, &key, &value);
        Require(copied.key.text != key.text && std::strcmp(_ZNK3sce4Json6String5c_strEv(&copied.key), "named") == 0 &&
                    copied.value.type == TypeInteger && copied.value.payload == 7,
                "Key-value pair construction must copy both members into independent storage");
        _ZN3sce4Json6Object4PairD1Ev(&copied);
        Require(trace.live.size() == baseline + 2, "Pair construction must release cleanly and keep only the key and value");
        _ZN3sce4Json6Object4PairC1Ev(&pair);
        _ZN3sce4Json6Object4PairD1Ev(&pair);
        _ZN3sce4Json5ValueD1Ev(&value);
        _ZN3sce4Json6StringD1Ev(&key);
        Require(trace.live.size() == baseline, "The pair block must release all owned resources");
    }

    // Allocation-failure budgets keep the iterator-state contract.
    {
        Array array{}; _ZN3sce4Json5ArrayC1Ev(&array);
        ArrayIterator result{};
        trace.budget = 0;
        _ZNK3sce4Json5Array5beginEv(&result, &array);
        trace.budget = -1;
        Require(result.state == nullptr, "A failed iterator-state allocation must leave the result unbound");
        errno = 71;
        _ZNK3sce4Json5Array5beginEv(&result, &array);
        Require(result.state != nullptr && *reinterpret_cast<void**>(result.state) != nullptr,
                "A retry after failure must publish an iterator bound to the empty-array sentinel like raw begin");
        _ZN3sce4Json5Array8iteratorD1Ev(&result);
        _ZN3sce4Json5ArrayD1Ev(&array);
        Require(trace.live.size() == baseline, "The budget block must release all owned resources");
        PreserveErrno("The budget block must preserve errno");
    }

    Require(trace.live.size() == initial, "Probe contract tests must release all owned resources");
    Require(trace.notifications == 1, "Only the deliberate budget failure may fire the out-of-memory notifier");
    return 0;
}
