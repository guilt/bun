#pragma once

#include "root.h"
#include "wtf/text/ASCIILiteral.h"
#include "wtf/SIMDUTF.h"

#include <JavaScriptCore/Error.h>
#include <JavaScriptCore/Exception.h>
#include <JavaScriptCore/Identifier.h>
#include <JavaScriptCore/JSGlobalObject.h>
#include <JavaScriptCore/JSString.h>
#include <JavaScriptCore/ThrowScope.h>
#include <JavaScriptCore/VM.h>
#include <limits>

namespace Zig {
class GlobalObject;
}

#include "headers-handwritten.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

extern "C" size_t Bun__stringSyntheticAllocationLimit;
extern "C" const char* Bun__errnoName(int);

namespace Zig {

// 8 bit byte
// we tag the final two bits
// so 56 bits are copied over
// rest we zero out for consistentcy
// On 32-bit targets the pointer is only 32 bits wide, so the tag bits move
// down to 28-31 (matching `bun_alloc::ZigString` on 32-bit): static=28,
// 32-bit ZigString flags-field bits (must match bun_alloc's Rust zs_tags):
// utf8=0x1, global=0x2, utf16=0x4, static=0x8.
#if !CPU(ADDRESS64)
constexpr unsigned char ZigStringFlagUTF8 = 0x1;
constexpr unsigned char ZigStringFlagGlobal = 0x2;
constexpr unsigned char ZigStringFlagUTF16 = 0x4;
constexpr unsigned char ZigStringFlagStatic = 0x8;
#endif

static const unsigned char* untag(const unsigned char* ptr)
{
#if CPU(ADDRESS64)
    return reinterpret_cast<const unsigned char*>(
        (((reinterpret_cast<uintptr_t>(ptr) & ~(static_cast<uint64_t>(1) << 63) & ~(static_cast<uint64_t>(1) << 62)) & ~(static_cast<uint64_t>(1) << 61)) & ~(static_cast<uint64_t>(1) << 60)));
#else
    // 32-bit: pointers are never tagged (flags live in the ZigString field).
    return ptr;
#endif
}

static void* untagVoid(const unsigned char* ptr)
{
    return const_cast<void*>(reinterpret_cast<const void*>(untag(ptr)));
}

static void* untagVoid(const char16_t* ptr)
{
    return untagVoid(reinterpret_cast<const unsigned char*>(ptr));
}

static bool isTaggedUTF16Ptr(const ZigString& str)
{
#if CPU(ADDRESS64)
    return (reinterpret_cast<uintptr_t>(str.ptr) & (static_cast<uint64_t>(1) << 63)) != 0;
#else
    return (str.flags & ZigStringFlagUTF16) != 0;
#endif
}

// Do we need to convert the string from UTF-8 to UTF-16?
static bool isTaggedUTF8Ptr(const ZigString& str)
{
#if CPU(ADDRESS64)
    return (reinterpret_cast<uintptr_t>(str.ptr) & (static_cast<uint64_t>(1) << 61)) != 0;
#else
    return (str.flags & ZigStringFlagUTF8) != 0;
#endif
}

static bool isTaggedExternalPtr(const ZigString& str)
{
#if CPU(ADDRESS64)
    return (reinterpret_cast<uintptr_t>(str.ptr) & (static_cast<uint64_t>(1) << 62)) != 0;
#else
    return (str.flags & ZigStringFlagGlobal) != 0;
#endif
}

static void free_global_string(void* str, void* ptr, unsigned len)
{
    // i don't understand why this happens
    if (ptr == nullptr)
        return;

    ZigString__freeGlobal(reinterpret_cast<const unsigned char*>(ptr), len);
}

// Switching to AtomString doesn't yield a perf benefit because we're recreating it each time.
static const WTF::String toString(ZigString str)
{
    if (str.len == 0 || str.ptr == nullptr) {
        return WTF::String();
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        ASSERT_WITH_MESSAGE(!isTaggedExternalPtr(str), "UTF8 and external ptr are mutually exclusive. The external will never be freed.");
        // Check if the resulting UTF-16 string could possibly exceed the maximum length.
        // For valid UTF-8, the number of UTF-16 code units is <= the number of UTF-8 bytes
        // (ASCII is 1:1; other code points use multiple UTF-8 bytes per UTF-16 code unit).
        // We only need to compute the actual UTF-16 length when the byte length exceeds the limit.
        size_t maxLength = std::min(Bun__stringSyntheticAllocationLimit, static_cast<size_t>(WTF::String::MaxLength));
        if (str.len > maxLength) [[unlikely]] {
            // UTF-8 byte length != UTF-16 length, so use simdutf to calculate the actual UTF-16 length.
            size_t utf16Length = simdutf::utf16_length_from_utf8(reinterpret_cast<const char*>(untag(str.ptr)), str.len);
            if (utf16Length > maxLength) {
                return {};
            }
        }
        return WTF::String::fromUTF8ReplacingInvalidSequences(std::span { untag(str.ptr), str.len });
    }

    if (isTaggedExternalPtr(str)) [[unlikely]] {
        // This will fail if the string is too long. Let's make it explicit instead of an ASSERT.
        if (str.len > Bun__stringSyntheticAllocationLimit || str.len > WTF::String::MaxLength) [[unlikely]] {
            free_global_string(nullptr, reinterpret_cast<void*>(const_cast<unsigned char*>(untag(str.ptr))), static_cast<unsigned>(str.len));
            return {};
        }

        return !isTaggedUTF16Ptr(str)
            ? WTF::String(WTF::ExternalStringImpl::create({ untag(str.ptr), str.len }, untagVoid(str.ptr), free_global_string))
            : WTF::String(WTF::ExternalStringImpl::create({ reinterpret_cast<const char16_t*>(untag(str.ptr)), str.len }, untagVoid(str.ptr), free_global_string));
    }

    // This will fail if the string is too long. Let's make it explicit instead of an ASSERT.
    if (str.len > Bun__stringSyntheticAllocationLimit || str.len > WTF::String::MaxLength) [[unlikely]] {
        return {};
    }

    return !isTaggedUTF16Ptr(str)
        ? WTF::String(WTF::StringImpl::createWithoutCopying({ untag(str.ptr), str.len }))
        : WTF::String(WTF::StringImpl::createWithoutCopying(
              { reinterpret_cast<const char16_t*>(untag(str.ptr)), str.len }));
}

static WTF::AtomString toAtomString(ZigString str)
{

    if (!isTaggedUTF16Ptr(str)) {
        return makeAtomString(std::span<const Latin1Character>(untag(str.ptr), str.len));
    } else {
        return makeAtomString(std::span<const char16_t>(reinterpret_cast<const char16_t*>(untag(str.ptr)), str.len));
    }
}

static const WTF::String toString(ZigString str, StringPointer ptr)
{
    if (str.len == 0 || str.ptr == nullptr || ptr.len == 0) {
        return WTF::String();
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        // Check if the resulting UTF-16 string could possibly exceed the maximum length.
        size_t maxLength = std::min(Bun__stringSyntheticAllocationLimit, static_cast<size_t>(WTF::String::MaxLength));
        if (ptr.len > maxLength) [[unlikely]] {
            size_t utf16Length = simdutf::utf16_length_from_utf8(reinterpret_cast<const char*>(&untag(str.ptr)[ptr.off]), ptr.len);
            if (utf16Length > maxLength) {
                return {};
            }
        }
        return WTF::String::fromUTF8ReplacingInvalidSequences(std::span { &untag(str.ptr)[ptr.off], ptr.len });
    }

    // This will fail if the string is too long. Let's make it explicit instead of an ASSERT.
    if (ptr.len > Bun__stringSyntheticAllocationLimit || ptr.len > WTF::String::MaxLength) [[unlikely]] {
        return {};
    }

    return !isTaggedUTF16Ptr(str)
        ? WTF::String(WTF::StringImpl::createWithoutCopying({ &untag(str.ptr)[ptr.off], ptr.len }))
        : WTF::String(WTF::StringImpl::createWithoutCopying(
              { &reinterpret_cast<const char16_t*>(untag(str.ptr))[ptr.off], ptr.len }));
}

static const WTF::String toStringCopy(ZigString str, StringPointer ptr)
{
    if (str.len == 0 || str.ptr == nullptr || ptr.len == 0) {
        return WTF::String();
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        // Check if the resulting UTF-16 string could possibly exceed the maximum length.
        size_t maxLength = std::min(Bun__stringSyntheticAllocationLimit, static_cast<size_t>(WTF::String::MaxLength));
        if (ptr.len > maxLength) [[unlikely]] {
            size_t utf16Length = simdutf::utf16_length_from_utf8(reinterpret_cast<const char*>(&untag(str.ptr)[ptr.off]), ptr.len);
            if (utf16Length > maxLength) {
                return {};
            }
        }
        return WTF::String::fromUTF8ReplacingInvalidSequences(std::span { &untag(str.ptr)[ptr.off], ptr.len });
    }

    // This will fail if the string is too long. Let's make it explicit instead of an ASSERT.
    if (ptr.len > Bun__stringSyntheticAllocationLimit || ptr.len > WTF::String::MaxLength) [[unlikely]] {
        return {};
    }

    return !isTaggedUTF16Ptr(str)
        ? WTF::String(WTF::StringImpl::create(std::span { &untag(str.ptr)[ptr.off], ptr.len }))
        : WTF::String(WTF::StringImpl::create(
              std::span { &reinterpret_cast<const char16_t*>(untag(str.ptr))[ptr.off], ptr.len }));
}

static const WTF::String toStringCopy(ZigString str)
{
    if (str.len == 0 || str.ptr == nullptr) {
        return WTF::String();
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        // Check if the resulting UTF-16 string could possibly exceed the maximum length.
        size_t maxLength = std::min(Bun__stringSyntheticAllocationLimit, static_cast<size_t>(WTF::String::MaxLength));
        if (str.len > maxLength) [[unlikely]] {
            size_t utf16Length = simdutf::utf16_length_from_utf8(reinterpret_cast<const char*>(untag(str.ptr)), str.len);
            if (utf16Length > maxLength) {
                return {};
            }
        }
        return WTF::String::fromUTF8ReplacingInvalidSequences(std::span { untag(str.ptr), str.len });
    }

    if (isTaggedUTF16Ptr(str)) {
        std::span<char16_t> out;
        auto impl = WTF::StringImpl::tryCreateUninitialized(str.len, out);
        if (!impl) [[unlikely]] {
            return WTF::String();
        }
        memcpy(out.data(), untag(str.ptr), str.len * sizeof(char16_t));
        return WTF::String(WTF::move(impl));
    } else {
        std::span<Latin1Character> out;
        auto impl = WTF::StringImpl::tryCreateUninitialized(str.len, out);
        if (!impl) [[unlikely]]
            return WTF::String();
        memcpy(out.data(), untag(str.ptr), str.len * sizeof(Latin1Character));
        return WTF::String(WTF::move(impl));
    }
}

static void appendToBuilder(ZigString str, WTF::StringBuilder& builder)
{
    if (str.len == 0 || str.ptr == nullptr) {
        return;
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        // Check if the resulting UTF-16 string could possibly exceed the maximum length.
        size_t maxLength = std::min(Bun__stringSyntheticAllocationLimit, static_cast<size_t>(WTF::String::MaxLength));
        if (str.len > maxLength) [[unlikely]] {
            size_t utf16Length = simdutf::utf16_length_from_utf8(reinterpret_cast<const char*>(untag(str.ptr)), str.len);
            if (utf16Length > maxLength) {
                return;
            }
        }
        WTF::String converted = WTF::String::fromUTF8ReplacingInvalidSequences(std::span { untag(str.ptr), str.len });
        builder.append(converted);
        return;
    }
    if (isTaggedUTF16Ptr(str)) {
        builder.append({ reinterpret_cast<const char16_t*>(untag(str.ptr)), str.len });
        return;
    }

    builder.append({ untag(str.ptr), str.len });
}

static WTF::String toStringNotConst(ZigString str) { return toString(str); }

static const JSC::JSString* toJSString(ZigString str, JSC::JSGlobalObject* global)
{
    return JSC::jsOwnedString(global->vm(), toString(str));
}

static JSC::JSString* toJSStringGC(ZigString str, JSC::JSGlobalObject* global)
{
    return JSC::jsString(global->vm(), toStringCopy(str));
}

static const ZigString ZigStringEmpty = ZigString { (unsigned char*)"", 0
#if !CPU(ADDRESS64)
    , 0
#endif
};
static const unsigned char __dot_char = '.';
static const ZigString ZigStringCwd = ZigString { &__dot_char, 1
#if !CPU(ADDRESS64)
    , 0
#endif
};
static const BunString BunStringCwd = BunString { BunStringTag::StaticZigString, ZigStringCwd };
static const BunString BunStringEmpty = BunString { BunStringTag::Empty, nullptr };

#if CPU(ADDRESS64)
static const unsigned char* taggedUTF16Ptr(const char16_t* ptr)
{
return reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(ptr) | (static_cast<uint64_t>(1) << 63));
}
#else
// 32-bit: pointers are never tagged; UTF-16 is marked via ZigString::flags.
static const unsigned char* taggedUTF16Ptr(const char16_t* ptr)
{
return reinterpret_cast<const unsigned char*>(ptr);
}
#endif

// ZigString construction with the UTF-16/UTF-8 kind flag. 64-bit tags the
// pointer; 32-bit sets the `flags` field.
#if CPU(ADDRESS64)
static ZigString makeZigString8(const unsigned char* data, size_t len)
{
    return ZigString { data, len };
}
static ZigString makeZigString16(const char16_t* data, size_t len)
{
    return ZigString { taggedUTF16Ptr(data), len };
}
#else
static ZigString makeZigString8(const unsigned char* data, size_t len)
{
    return ZigString { data, len, 0 };
}
static ZigString makeZigString16(const char16_t* data, size_t len)
{
    return ZigString { reinterpret_cast<const unsigned char*>(data), len, ZigStringFlagUTF16 };
}
#endif

static ZigString toZigString(WTF::String* str)
{
    return str->isEmpty()
        ? ZigStringEmpty
        : str->is8Bit() ? makeZigString8(str->span8().data(), str->length()) : makeZigString16(str->span16().data(), str->length());
}

static ZigString toZigString(WTF::StringImpl& str)
{
    return str.isEmpty()
        ? ZigStringEmpty
        : str.is8Bit() ? makeZigString8(str.span8().data(), str.length()) : makeZigString16(str.span16().data(), str.length());
}

// Overload for `StringImpl*` so callers like `toZigString(string.impl())` resolve here
// instead of implicitly constructing a temporary `WTF::StringView` (which, in debug builds
// with CHECK_STRINGVIEW_LIFETIME, takes a lock and heap-allocates an UnderlyingString entry).
static ZigString toZigString(const WTF::StringImpl* str)
{
    return (!str || str->isEmpty())
        ? ZigStringEmpty
        : str->is8Bit() ? makeZigString8(str->span8().data(), str->length()) : makeZigString16(str->span16().data(), str->length());
}

static ZigString toZigString(WTF::StringView& str)
{
    return str.isEmpty()
        ? ZigStringEmpty
        : str.is8Bit() ? makeZigString8(str.span8().data(), str.length()) : makeZigString16(str.span16().data(), str.length());
}

static ZigString toZigString(const WTF::StringView& str)
{
    return str.isEmpty()
        ? ZigStringEmpty
        : str.is8Bit() ? makeZigString8(str.span8().data(), str.length()) : makeZigString16(str.span16().data(), str.length());
}

static ZigString toZigString(JSC::JSString& str, JSC::JSGlobalObject* global)
{
    if (str.isSubstring()) {
        return toZigString(str.view(global));
    }

    return toZigString(str.value(global));
}

static ZigString toZigString(JSC::JSString* str, JSC::JSGlobalObject* global)
{
    if (str->isSubstring()) {
        return toZigString(str->view(global));
    }
    return toZigString(str->value(global));
}

static ZigString toZigString(JSC::Identifier& str, JSC::JSGlobalObject* global)
{
    return toZigString(str.string());
}

static ZigString toZigString(JSC::Identifier* str, JSC::JSGlobalObject* global)
{
    return toZigString(str->string());
}

static WTF::StringView toStringView(ZigString str)
{
    return WTF::StringView(std::span { untag(str.ptr), str.len });
}

static void throwException(JSC::ThrowScope& scope, ZigErrorType err, JSC::JSGlobalObject* global)
{
    scope.throwException(global,
        JSC::Exception::create(global->vm(), JSC::JSValue::decode(err.value)));
}

static ZigString toZigString(JSC::JSValue val, JSC::JSGlobalObject* global)
{
    auto scope = DECLARE_THROW_SCOPE(global->vm());
    auto* str = val.toString(global);

    if (scope.exception()) [[unlikely]] {
        (void)scope.tryClearException();
        scope.release();
        return ZigStringEmpty;
    }

    auto view = str->view(global);
    if (scope.exception()) [[unlikely]] {
        (void)scope.tryClearException();
        scope.release();
        return ZigStringEmpty;
    }

    return toZigString(view);
}

static const WTF::String toStringStatic(ZigString str)
{
    if (str.len == 0 || str.ptr == nullptr) {
        return WTF::String();
    }
    if (isTaggedUTF8Ptr(str)) [[unlikely]] {
        abort();
    }

    if (isTaggedUTF16Ptr(str)) {
        return WTF::String(AtomStringImpl::add(std::span { reinterpret_cast<const char16_t*>(untag(str.ptr)), str.len }));
    }

    // Rust `&'static str` / `&'static [u8]` literals are NOT null-terminated,
    // so the previous `ASCIILiteral::fromLiteralUnsafe` path (which `strlen`s
    // and asserts a trailing NUL) over-reads. Intern via a length-bounded span
    // instead — same atom-table caching, no NUL dependency.
    auto* untagged = untag(str.ptr);
    return WTF::String(AtomStringImpl::add(std::span { untagged, str.len }));
}

static JSC::JSValue getErrorInstance(const ZigString* str, JSC::JSGlobalObject* globalObject)
{
    WTF::String message = toString(*str);
    if (message.isNull() && str->len > 0) [[unlikely]] {
        // pending exception while creating an error.
        return {};
    }

    JSC::JSObject* result = JSC::createError(globalObject, message);
    JSC::EnsureStillAliveScope ensureAlive(result);

    return result;
}

static JSC::JSValue getTypeErrorInstance(const ZigString* str, JSC::JSGlobalObject* globalObject)
{
    JSC::JSObject* result = JSC::createTypeError(globalObject, toStringCopy(*str));
    JSC::EnsureStillAliveScope ensureAlive(result);

    return result;
}

static JSC::JSValue getSyntaxErrorInstance(const ZigString* str, JSC::JSGlobalObject* globalObject)
{
    JSC::JSObject* result = JSC::createSyntaxError(globalObject, toStringCopy(*str));
    JSC::EnsureStillAliveScope ensureAlive(result);

    return result;
}

static JSC::JSValue getRangeErrorInstance(const ZigString* str, JSC::JSGlobalObject* globalObject)
{
    JSC::JSObject* result = JSC::createRangeError(globalObject, toStringCopy(*str));
    JSC::EnsureStillAliveScope ensureAlive(result);

    return result;
}

static const JSC::Identifier toIdentifier(ZigString str, JSC::JSGlobalObject* global)
{
    if (str.len == 0 || str.ptr == nullptr) {
        return global->vm().propertyNames->emptyIdentifier;
    }
    WTF::String wtfstr = Zig::isTaggedExternalPtr(str) ? toString(str) : Zig::toStringCopy(str);
    JSC::Identifier id = JSC::Identifier::fromString(global->vm(), wtfstr);
    return id;
}

}; // namespace Zig

JSC::JSValue createSystemError(JSC::JSGlobalObject* global, ASCIILiteral message, ASCIILiteral syscall, int err);
JSC::JSValue createSystemError(JSC::JSGlobalObject* global, ASCIILiteral syscall, int err);

static void throwSystemError(JSC::ThrowScope& scope, JSC::JSGlobalObject* globalObject, ASCIILiteral syscall, int err)
{
    scope.throwException(globalObject, createSystemError(globalObject, syscall, err));
}

static void throwSystemError(JSC::ThrowScope& scope, JSC::JSGlobalObject* globalObject, ASCIILiteral message, ASCIILiteral syscall, int err)
{
    scope.throwException(globalObject, createSystemError(globalObject, message, syscall, err));
}

template<typename WebCoreType, typename OutType>
OutType* WebCoreCast(JSC::EncodedJSValue JSValue0)
{
    // we must use jsDynamicCast here so that we check that the type is correct
    WebCoreType* jsdomURL = dynamicDowncast<WebCoreType>(JSC::JSValue::decode(JSValue0));
    if (jsdomURL == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<OutType*>(&jsdomURL->wrapped());
}

#pragma clang diagnostic pop
