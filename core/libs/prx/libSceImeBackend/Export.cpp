#include "prx/libSceImeBackend/include/Api.hpp"
#include "prx/libSceIme/Export.cpp"
#include "prx/libc/include/General.hpp"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {

constexpr int kAlreadyInitialized = static_cast<int>(0x80bc1001);
constexpr int kNotInitialized = static_cast<int>(0x80bc1002);
constexpr int kInvalidMode = static_cast<int>(0x80bc1010);
constexpr int kInvalidLength = static_cast<int>(0x80bc1011);
constexpr int kInvalidType = static_cast<int>(0x80bc1012);
constexpr int kInvalidReservedArgument = static_cast<int>(0x80bc1014);
constexpr int kInvalidStringSelector = static_cast<int>(0x80bc1030);
constexpr int kNullArgument = static_cast<int>(0x80bc1031);
constexpr int kInvalidReservedField = static_cast<int>(0x80bc1032);
constexpr int kInvalidState = static_cast<int>(0x80bc1033);
constexpr int kInitialStringTooLong = static_cast<int>(0x80bc1034);
constexpr int kInvalidField18 = static_cast<int>(0x80bc1035);
constexpr int kNotAvailable = static_cast<int>(0x80bc10ff);
constexpr std::size_t kTextCapacity = 0x5a;
constexpr std::size_t kInitialStringLimit = 0x3ff;

struct State {
    std::u16string text;
    std::size_t caret = 0;
    int convertMode = 0;
    int status = 0;
    int maxLength = 0;
    int type = 0;
};

std::mutex g_mutex;
std::unique_ptr<State> g_state;

bool CopyText(const std::u16string& source, char16_t* output, std::size_t capacity) {
    const std::size_t count = std::min(source.size(), capacity);
    const int savedErrno = errno;
    if (count != 0 && !GuestCopyOut_nid_no_patch(output, source.data(), count * sizeof(char16_t))) {
        errno = savedErrno;
        return false;
    }
    const char16_t terminator = 0;
    const bool written = GuestCopyOut_nid_no_patch(output + count, &terminator, sizeof(terminator));
    errno = savedErrno;
    return written;
}

bool ProbeParam(const SceImeBackendParam* param, SceImeBackendParam& local) {
    if (param == nullptr) return false;
    const int savedErrno = errno;
    const bool read = GuestCopyIn_nid_no_patch(&local, param, sizeof(SceImeBackendParam));
    errno = savedErrno;
    return read;
}

// Reads the guest UTF-16 initial string the way the raw length scan sees it:
// a terminator within the limit admits the text, a longer run reports
// too-long, an unreadable buffer refuses so Open never faults.
int ReadInitialString(const char16_t* source, std::u16string& destination) {
    char16_t chunk[0x40];
    destination.clear();
    for (std::size_t offset = 0; offset <= kInitialStringLimit; offset += std::size(chunk)) {
        const std::size_t units = std::min<std::size_t>(std::size(chunk), kInitialStringLimit + 1 - offset);
        const int savedErrno = errno;
        const bool read = GuestCopyIn_nid_no_patch(chunk, source + offset, units * sizeof(char16_t));
        errno = savedErrno;
        if (!read) return kNullArgument;
        const char16_t* end = std::find(chunk, chunk + units, char16_t{0});
        destination.append(chunk, end - chunk);
        if (end != chunk + units) return 0;
    }
    return kInitialStringTooLong;
}

int ValidateParam(const SceImeBackendParam& param, bool initialStringTooLong) {
    if (param.mode == -1 || param.mode == 0xff) return kInvalidMode;
    if (param.maxLength < 1 || param.maxLength > 0x1b) return kInvalidLength;
    if (param.type < 0 || param.type > 3) return kInvalidType;
    if (initialStringTooLong) return kInitialStringTooLong;
    if (param.field18 < 0 || param.field18 > 1) return kInvalidField18;
    for (const std::uint8_t value : param.reserved) {
        if (value != 0) return kInvalidReservedField;
    }
    return 0;
}

State* CurrentState() {
    return g_state.get();
}

}

extern "C" {

void APS5_VABI sceImeBackendParamInit(void* param) {
    if (param == nullptr) return;
    unsigned char local[0x28] = {};
    std::memset(local, 0xff, 4);
    const int savedErrno = errno;
    GuestCopyOut_nid_no_patch(param, local, sizeof(local));
    errno = savedErrno;
}

int APS5_VABI sceImeBackendOpen(const SceImeBackendParam* param, void* reserved) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state != nullptr) return kAlreadyInitialized;
    if (param == nullptr) return kNullArgument;
    if (reserved != nullptr) return kInvalidReservedArgument;
    SceImeBackendParam local{};
    if (!ProbeParam(param, local)) return kNullArgument;
    std::u16string initialText;
    bool initialStringTooLong = false;
    bool initialStringUnreadable = false;
    if ((local.type & 2) != 0 && local.initialString != nullptr) {
        const int status = ReadInitialString(local.initialString, initialText);
        if (status == kInitialStringTooLong) initialStringTooLong = true;
        else if (status != 0) initialStringUnreadable = true;
    }
    const int result = ValidateParam(local, initialStringTooLong);
    if (result != 0) return result;
    if (initialStringUnreadable) return kNullArgument;
    auto state = std::make_unique<State>();
    state->maxLength = local.maxLength;
    state->type = local.type;
    state->text = std::move(initialText);
    state->caret = state->text.size();
    g_state = std::move(state);
    return 0;
}

int APS5_VABI sceImeBackendClose(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state == nullptr) return kNotInitialized;
    g_state.reset();
    return 0;
}

int APS5_VABI sceImeBackendReset(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    state->text.clear();
    state->caret = 0;
    state->convertMode = 0;
    state->status = 0;
    return 0;
}

int APS5_VABI sceImeBackendGetStatus(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const State* state = CurrentState();
    if (state == nullptr) return 0;
    return state->status < 4 ? state->status + 1 : 0;
}

int APS5_VABI sceImeBackendEnterCharacter(std::uint32_t character, char16_t* output) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (output == nullptr) return kNullArgument;
    if (state->status >= 2) return kInvalidState;
    if (character > 0x7f) {
        NotImplemented_nid_no_patch(__func__);
        return 0;
    }
    if (state->text.size() >= static_cast<std::size_t>(state->maxLength)) return kNotAvailable;
    state->text.insert(state->text.begin() + static_cast<std::ptrdiff_t>(state->caret),
                       static_cast<char16_t>(character));
    ++state->caret;
    if (!CopyText(state->text, output, kTextCapacity)) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendBackspaceCharacter(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kInvalidState;
    if (state->caret == 0) return 0;
    state->text.erase(state->caret - 1, 1);
    --state->caret;
    if (state->caret > state->text.size()) state->caret = state->text.size();
    return 0;
}

int APS5_VABI sceImeBackendPartConfirm(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendAllConfirm(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendAllDeleteConvertString(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status > 3) return kNotAvailable;
    state->text.clear();
    state->caret = 0;
    state->status = 0;
    return 0;
}

int APS5_VABI sceImeBackendConvertForward(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendConvertBackward(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendConvertCancel(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendGetString(int which, char16_t* output, std::uint16_t capacity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (output == nullptr) return kNullArgument;
    if (which != 0 && which != 1) return kInvalidStringSelector;
    if (!CopyText(state->text, output, capacity)) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendGetConvertString(char16_t* output) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (output == nullptr) return kNullArgument;
    if (!CopyText(state->text, output, kTextCapacity)) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendGetConfirmString(char16_t* output) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (output == nullptr) return kNullArgument;
    if (!CopyText(state->text, output, kTextCapacity)) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendGetCaretIndex(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kNotAvailable;
    return static_cast<int>(state->caret);
}

int APS5_VABI sceImeBackendModeCaretNext(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kInvalidState;
    if (state->caret < state->text.size()) ++state->caret;
    return 0;
}

int APS5_VABI sceImeBackendModeCaretPrevious(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kInvalidState;
    if (state->caret != 0) --state->caret;
    return 0;
}

int APS5_VABI sceImeBackendGetCandidateList(std::uint16_t* selected, char16_t* output) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (selected == nullptr || output == nullptr) return kNullArgument;
    if (state->status >= 2) return kNotAvailable;
    const std::uint16_t none = 0;
    const char16_t terminator = 0;
    const int savedErrno = errno;
    if (!GuestCopyOut_nid_no_patch(selected, &none, sizeof(none))) {
        errno = savedErrno;
        return kNullArgument;
    }
    const bool written = GuestCopyOut_nid_no_patch(output, &terminator, sizeof(terminator));
    errno = savedErrno;
    if (!written) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendGetCandidateListSize(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kNotAvailable;
    return 0;
}

int APS5_VABI sceImeBackendGetCandidateSelect(std::uint16_t* selected) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (selected == nullptr) return kNullArgument;
    if (state->status >= 2) return kNotAvailable;
    const std::uint16_t none = 0;
    const int savedErrno = errno;
    const bool written = GuestCopyOut_nid_no_patch(selected, &none, sizeof(none));
    errno = savedErrno;
    if (!written) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendGetConvertMode(int* mode) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (mode == nullptr) return kNullArgument;
    const int savedErrno = errno;
    const bool written = GuestCopyOut_nid_no_patch(mode, &state->convertMode, sizeof(state->convertMode));
    errno = savedErrno;
    if (!written) return kNullArgument;
    return 0;
}

int APS5_VABI sceImeBackendTemporalConfirm(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceImeBackendSetConvertMode(int mode) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    state->convertMode = mode;
    return 0;
}

int APS5_VABI sceImeBackendForTestFunction(void) {
    return kNotAvailable;
}

int APS5_VABI sceImeBackendDeleteCharacter(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State* state = CurrentState();
    if (state == nullptr) return kNotInitialized;
    if (state->status >= 2) return kInvalidState;
    if (state->caret >= state->text.size()) return 0;
    state->text.erase(state->caret, 1);
    return 0;
}

}
