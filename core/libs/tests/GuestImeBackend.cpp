#include "prx/libSceImeBackend/include/Api.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace {

constexpr int kNullArgument = static_cast<int>(0x80bc1031);
constexpr int kInitialStringTooLong = static_cast<int>(0x80bc1034);

void Require(bool value) { if (!value) std::abort(); }

void CheckUnreadableOpen() {
    // An unreadable parameter must refuse like the raw null argument instead
    // of faulting the host, preserving the caller's errno.
    errno = 71;
    Require(sceImeBackendOpen(reinterpret_cast<const SceImeBackendParam*>(0x10), nullptr) == kNullArgument);
    Require(errno == 71);
    // A readable parameter with an unreadable initial string refuses too.
    const char16_t* const unreadableText = reinterpret_cast<const char16_t*>(0x10);
    SceImeBackendParam parameter{0, 8, 2, unreadableText, 0, {}};
    errno = 71;
    Require(sceImeBackendOpen(&parameter, nullptr) == kNullArgument);
    Require(errno == 71);
    // The readable probe keeps raw's validation precedence: an invalid mode
    // wins over anything the string would say.
    SceImeBackendParam badMode{0xff, 8, 2, unreadableText, 0, {}};
    errno = 71;
    Require(sceImeBackendOpen(&badMode, nullptr) == static_cast<int>(0x80bc1010));
    Require(errno == 71);
}

void CheckInitialStringBounds() {
    // A run with no terminator inside the limit reports too-long exactly like
    // the raw length scan, without reading past the limit.
    static char16_t longText[0x400];
    for (char16_t& unit : longText) unit = 0x41;
    SceImeBackendParam parameter{0, 8, 2, longText, 0, {}};
    errno = 71;
    Require(sceImeBackendOpen(&parameter, nullptr) == kInitialStringTooLong);
    Require(errno == 71);
    // A terminator exactly at the limit admits the text.
    static char16_t limitText[0x400];
    for (std::size_t index = 0; index < 0x3ff; ++index) limitText[index] = 0x41;
    limitText[0x3ff] = 0;
    SceImeBackendParam limit{0, 8, 2, limitText, 0, {}};
    Require(sceImeBackendOpen(&limit, nullptr) == 0);
    char16_t output[0x5d]{};
    Require(sceImeBackendGetString(0, output, 0x5c) == 0);
    const std::u16string_view text{output};
    Require(text.size() == 0x5c);
    Require(sceImeBackendClose() == 0);
}

void CheckUnwritableOutputs() {
    // The query outputs write through checked copies and refuse unwritable
    // pointers like the raw null argument.
    errno = 71;
    SceImeBackendParam parameter{0, 8, 0, nullptr, 0, {}};
    Require(sceImeBackendOpen(&parameter, nullptr) == 0);
    auto* unwritable = reinterpret_cast<std::uint16_t*>(0x10);
    Require(sceImeBackendGetCandidateList(unwritable, reinterpret_cast<char16_t*>(0x12)) == kNullArgument);
    Require(errno == 71);
    Require(sceImeBackendGetCandidateSelect(unwritable) == kNullArgument);
    Require(errno == 71);
    Require(sceImeBackendGetConvertMode(reinterpret_cast<int*>(0x10)) == kNullArgument);
    Require(errno == 71);
    Require(sceImeBackendEnterCharacter(0x41, reinterpret_cast<char16_t*>(0x10)) == kNullArgument);
    Require(errno == 71);
    Require(sceImeBackendGetString(0, reinterpret_cast<char16_t*>(0x10), 0x5c) == kNullArgument);
    Require(errno == 71);
    // Readable outputs keep the values.
    std::uint16_t selected = 0xffff;
    char16_t candidate[4] = {0x42, 0x42, 0x42, 0x42};
    Require(sceImeBackendGetCandidateList(&selected, candidate) == 0);
    Require(selected == 0 && candidate[0] == 0 && candidate[1] == 0x42);
    Require(sceImeBackendGetCandidateSelect(&selected) == 0 && selected == 0);
    int mode = 0x2a;
    Require(sceImeBackendGetConvertMode(&mode) == 0 && mode == 0);
    // The text round-trip through the probed copy stays intact. The refused
    // EnterCharacter above already inserted its character (raw mutates the
    // text before publishing the output), so this one appends to "A".
    Require(sceImeBackendEnterCharacter(0x41, candidate) == 0);
    Require(candidate[0] == 0x41 && candidate[1] == 0x41 && candidate[2] == 0 && candidate[3] == 0x42);
    char16_t text[8]{0x43, 0x43, 0x43, 0x43};
    Require(sceImeBackendGetString(0, text, 4) == 0);
    Require(text[0] == 0x41 && text[1] == 0x41 && text[2] == 0 && text[3] == 0x43);
    Require(sceImeBackendClose() == 0);
    Require(errno == 71);
}

void CheckParamInit() {
    sceImeBackendParamInit(nullptr);
    errno = 71;
    sceImeBackendParamInit(reinterpret_cast<void*>(0x10));
    Require(errno == 71);
    for (const unsigned char fill : {static_cast<unsigned char>(0xaa), static_cast<unsigned char>(0x00), static_cast<unsigned char>(0xff)}) {
        std::array<unsigned char, 0x40> buffer{};
        buffer.fill(fill);
        sceImeBackendParamInit(buffer.data() + 8);
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            const bool inside = i >= 8 && i < 8 + 0x28;
            const unsigned char expected = !inside ? fill : i < 12 ? 0xff : 0;
            Require(buffer[i] == expected);
        }
        for (std::size_t i = 8; i < 8 + 0x28; ++i) buffer[i] = 0x5Au;
        sceImeBackendParamInit(buffer.data() + 8);
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            const bool inside = i >= 8 && i < 8 + 0x28;
            const unsigned char expected = !inside ? fill : i < 12 ? 0xff : 0;
            Require(buffer[i] == expected);
        }
    }
}

void CheckForTestFunction() {
    for (int call = 0; call < 3; ++call) {
        Require(sceImeBackendForTestFunction() == static_cast<int>(0x80bc10ffu));
    }
}

}

int main() {
    CheckParamInit();
    CheckForTestFunction();
    CheckUnreadableOpen();
    CheckInitialStringBounds();
    CheckUnwritableOutputs();
    return 0;
}
