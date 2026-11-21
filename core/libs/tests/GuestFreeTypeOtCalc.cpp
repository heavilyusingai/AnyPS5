#include "prx/libSceFreeTypeOt/include/Calc.hpp"
#include "prx/libc/include/General.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {

using FreeType::GuestGlyphMetrics;
using FreeType::GuestMatrix;
using FreeType::GuestVector;

void Require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FreeType Ot calc: %s\n", message);
    std::exit(1);
}

template <typename TCall>
bool Invalid(TCall call) {
    errno = 71;
    try {
        call();
    } catch (const std::invalid_argument&) {
        return errno == 71;
    }
    return false;
}

void VectorTransform() {
    GuestVector vector{1 << 16, 2 << 16};
    const GuestMatrix identity{1 << 16, 0, 0, 1 << 16};
    errno = 71;
    FT_Vector_Transform_Scaled_nid_postfix(&vector, &identity, 1);
    Require(errno == 71, "a readable transform keeps the caller errno");
    Require(vector.x == 1 << 16 && vector.y == 2 << 16, "an identity matrix keeps the vector");
    const GuestMatrix doubleScale{2 << 16, 0, 0, 2 << 16};
    FT_Vector_Transform_Scaled_nid_postfix(&vector, &doubleScale, 1);
    Require(vector.x == 2 << 16 && vector.y == 4 << 16, "a doubled matrix scales the vector");
    const GuestMatrix shear{1 << 16, 1 << 16, 0, 1 << 16};
    FT_Vector_Transform_Scaled_nid_postfix(&vector, &shear, 1);
    Require(vector.x == (2 << 16) + (4 << 16) && vector.y == 4 << 16, "a shearing matrix sums the scaled terms");

    GuestVector untouched{3 << 16, 4 << 16};
    errno = 71;
    FT_Vector_Transform_Scaled_nid_postfix(&untouched, &identity, 1);
    Require(errno == 71, "a readable transform keeps the caller errno before the write");
    FT_Vector_Transform_Scaled_nid_postfix(nullptr, &doubleScale, 1);
    FT_Vector_Transform_Scaled_nid_postfix(&untouched, nullptr, 1);
    Require(untouched.x == 3 << 16 && untouched.y == 4 << 16, "a null vector or matrix is a no-op like raw");
    Require(Invalid([&] { FT_Vector_Transform_Scaled_nid_postfix(reinterpret_cast<GuestVector*>(0x10), &identity, 1); }), "an unreadable vector refuses with Invalid_Argument");
    Require(Invalid([&] { FT_Vector_Transform_Scaled_nid_postfix(&untouched, reinterpret_cast<const GuestMatrix*>(0x10), 1); }), "an unreadable matrix refuses with Invalid_Argument");
}

void Synthesize() {
    GuestGlyphMetrics metrics{};
    metrics.width = 100;
    metrics.height = 400;
    metrics.horiBearingX = 30;
    metrics.horiBearingY = 200;
    metrics.horiAdvance = 500;
    errno = 71;
    ft_synthesize_vertical_metrics_nid_postfix(&metrics, 0);
    Require(errno == 71, "a readable synthesis keeps the caller errno");
    Require(metrics.vertBearingX == 30 - 250, "vertBearingX mirrors the horizontal bearing minus half the advance");
    Require(metrics.vertAdvance == 240, "a zero advance derives from the height minus the bearing");
    Require(metrics.vertBearingY == (metrics.vertAdvance - 200) / 2, "vertBearingY centres the height in the advance");

    GuestGlyphMetrics explicitAdvance{};
    explicitAdvance.height = 100;
    explicitAdvance.horiBearingY = -50;
    explicitAdvance.horiBearingX = 10;
    explicitAdvance.horiAdvance = 80;
    ft_synthesize_vertical_metrics_nid_postfix(&explicitAdvance, 300);
    Require(explicitAdvance.vertAdvance == 300, "an explicit advance is used verbatim");
    Require(explicitAdvance.vertBearingY == (300 - 100) / 2, "a negative bearing raises the height");
    Require(explicitAdvance.vertBearingX == 10 - 40, "vertBearingX uses the horizontal values");

    GuestGlyphMetrics untouched{};
    untouched.height = 7;
    ft_synthesize_vertical_metrics_nid_postfix(nullptr, 5);
    Require(untouched.height == 7, "a null metrics pointer is a no-op like raw");
    Require(Invalid([&] { ft_synthesize_vertical_metrics_nid_postfix(reinterpret_cast<GuestGlyphMetrics*>(0x10), 5); }), "an unreadable metrics pointer refuses with Invalid_Argument");
}

void ServiceLookup() {
    GuestServiceDesc list[3];
    static char nameBuffer[] = "glyph-to-bitmap-map";
    static char otherBuffer[] = "outline-glyph-to-bitmap";
    static int payload = 7;
    list[0] = {nameBuffer, &payload};
    list[1] = {otherBuffer, nullptr};
    list[2] = {nullptr, nullptr};
    errno = 71;
    Require(ft_service_list_lookup_nid_postfix(list, "outline-glyph-to-bitmap") == nullptr, "the second descriptor's null data is returned verbatim");
    Require(errno == 71, "a readable lookup keeps the caller errno");
    Require(ft_service_list_lookup_nid_postfix(list, "glyph-to-bitmap-map") == &payload, "a matching service id returns its data");
    Require(ft_service_list_lookup_nid_postfix(list, "missing-service") == nullptr, "a missing service id returns null");
    Require(ft_service_list_lookup_nid_postfix(nullptr, "glyph-to-bitmap-map") == nullptr, "a null descriptor list returns null like raw");
    Require(ft_service_list_lookup_nid_postfix(list, nullptr) == nullptr, "a null service id returns null like raw");
    errno = 71;
    Require(ft_service_list_lookup_nid_postfix(list, reinterpret_cast<const char*>(0x10)) == nullptr, "an unreadable service id returns null");
    Require(errno == 71, "an unreadable service id keeps the caller errno");
    const GuestServiceDesc unreadable[2] = {{nameBuffer, &payload}, {reinterpret_cast<const char*>(0x10), nullptr}};
    Require(ft_service_list_lookup_nid_postfix(unreadable, "missing-service") == nullptr, "an unreadable descriptor's service id stops the walk");
    char unterminated[129];
    std::memset(unterminated, 'a', 128);
    unterminated[128] = 0;
    GuestServiceDesc unterminatedList[2] = {{unterminated, &payload}, {nullptr, nullptr}};
    Require(ft_service_list_lookup_nid_postfix(unterminatedList, unterminated) == nullptr, "a service id without a terminator inside the bound refuses the lookup");
}

}

int main() {
    VectorTransform();
    Synthesize();
    ServiceLookup();
    std::puts("Guest FreeType Ot calc tests passed");
    return 0;
}
