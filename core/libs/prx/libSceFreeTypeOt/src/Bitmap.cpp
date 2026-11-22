// Derived from FreeType src/base/ftbitmap.c (FreeType 2.4.12, commit cecf93ef90c660a8b0b45e5adbbfb5ea443fb6b9).
// Copyright 2004-2009, 2011, 2013 by David Turner, Robert Wilhelm, and Werner Lemberg.
// This file is part of the FreeType project, and may only be used, modified, and distributed
// under the terms of the FreeType project license, LICENSE.TXT. By continuing to use, modify,
// or distribute this file you indicate that you have read the license and understand and accept it fully.

#include "prx/libSceFreeTypeOt/include/Bitmap.hpp"
#include "prx/libSceFreeTypeOt/include/GlyphSlot.hpp"
#include "prx/libSceFreeTypeOt/include/Memory.hpp"
#include "prx/libc/include/General.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace FreeType;

namespace {

constexpr std::int64_t IntMax = std::numeric_limits<std::int32_t>::max();
constexpr std::int64_t IntMin = std::numeric_limits<std::int32_t>::min();

std::int32_t CopyBitmap(GuestBitmapLibrary* library, const GuestBitmap* source, GuestBitmap* target, const char* function) {
    if (!library) NotImplemented_nid_no_patch(function);
    GuestMemoryRec* memory = library->memory;
    if (!source->buffer) {
        *target = *source;
        return 0;
    }
    std::int64_t pitch = source->pitch;
    if (pitch == IntMin) NotImplemented_nid_no_patch(function);
    if (pitch < 0) pitch = -pitch;
    const std::int64_t size = pitch * static_cast<std::int32_t>(source->rows);
    if (size > IntMax || size < IntMin) NotImplemented_nid_no_patch(function);
    if (size > 0 && !memory) NotImplemented_nid_no_patch(function);
    std::int32_t error = 0;
    auto* buffer = static_cast<unsigned char*>(OtMemQAlloc(memory, size, &error));
    if (!error) {
        *target = *source;
        target->buffer = buffer;
        if (size > 0) std::memcpy(buffer, source->buffer, static_cast<std::size_t>(size));
    }
    return error;
}

}

namespace {
constexpr int InvalidArgument = 0x06;
}

extern "C" {

int APS5_VABI sceFreeTypeOtGlyphSlotOwnBitmap_nid_no_patch(GuestGlyphSlotRec* slot) {
    const char* function = __func__;
    GuestGlyphSlotRec local{};
    if (!slot) return 0;
    {
        const int savedErrno = errno;
        const bool readable = GuestCopyIn_nid_no_patch(&local, slot, sizeof(local));
        errno = savedErrno;
        if (!readable || local.format != GlyphFormatBitmap) return 0;
    }
    GuestSlotInternalRec internal{};
    if (!local.internal) NotImplemented_nid_no_patch(function);
    {
        const int savedErrno = errno;
        const bool readable = GuestCopyIn_nid_no_patch(&internal, static_cast<GuestSlotInternalRec*>(local.internal), sizeof(internal));
        errno = savedErrno;
        if (!readable) NotImplemented_nid_no_patch(function);
    }
    if (internal.flags & GlyphOwnBitmap) return 0;
    GuestBitmap bitmap{};
    const std::int32_t error = CopyBitmap(static_cast<GuestBitmapLibrary*>(local.library), &local.bitmap, &bitmap, function);
    if (error) return error;
    {
        const int savedWriteErrno = errno;
        const bool published = GuestCopyOut_nid_no_patch(&slot->bitmap, &bitmap, sizeof(bitmap));
        errno = savedWriteErrno;
        if (!published) return InvalidArgument;
        internal.flags |= GlyphOwnBitmap;
        const int savedFlagsErrno = errno;
        const bool flagsWritten = GuestCopyOut_nid_no_patch(static_cast<GuestSlotInternalRec*>(local.internal), &internal, sizeof(internal));
        errno = savedFlagsErrno;
        if (!flagsWritten) return InvalidArgument;
    }
    return 0;
}
APS5_EXPORT("nSIux4OQyJM", sceFreeTypeOtGlyphSlotOwnBitmap_nid_no_patch);

}
