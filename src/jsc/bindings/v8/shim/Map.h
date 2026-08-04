#pragma once

#include "TaggedPointer.h"

namespace v8 {
namespace shim {

enum class InstanceType : uint16_t {
    // v8-internal.h:787, kFirstNonstringType is 0x80
    String = 0x7f,
    // "Oddball" in V8 means undefined or null
    // v8-internal.h:788
    Oddball = 0x83,
    // v8-internal.h:1016 kFirstNonstringType
    // this cannot be kJSObjectType (or anything in the range [kJSObjectType, kLastJSApiObjectType])
    // because then V8 will try to access internal fields directly instead of calling
    // SlowGetInternalField
    Object = 0x80,
    // a number that doesn't fit in int32_t and is stored on the heap (for us, in the
    // HandleScopeBuffer)
    HeapNumber = 0x82,
};

// V8's description of the structure of an object
struct Map {
    // Layout matching V8 64-bit Map even on 32-bit, because the real
    // V8 headers (from Node.js) are for the 64-bit host. On 32-bit
    // TaggedPointer is 4 bytes, so pad to 8 with m_pad0.
    TaggedPointer m_metaMap;
#if !CPU(ADDRESS64)
    uint32_t m_pad0;
#endif
    uint32_t m_unused;
    InstanceType m_instanceType;

    static const Map& map_map();
    static const Map& object_map();
    static const Map& oddball_map();
    static const Map& string_map();
    static const Map& heap_number_map();

    Map(InstanceType instance_type)
        : m_metaMap(const_cast<Map*>(&map_map()))
#if !CPU(ADDRESS64)
        , m_pad0(0)
#endif
        , m_unused(0xaaaaaaaa)
        , m_instanceType(instance_type)
    {
    }

    enum class MapMapTag {
        MapMap
    };

    Map(MapMapTag)
        : m_metaMap(this)
#if !CPU(ADDRESS64)
        , m_pad0(0)
#endif
        , m_unused(0xaaaaaaaa)
        , m_instanceType(InstanceType::Object)
    {
    }
};

static_assert(sizeof(Map) == 16, "Map has wrong layout");
static_assert(offsetof(Map, m_instanceType) == 12, "Map has wrong layout");
static_assert(offsetof(Map, m_metaMap) == 0, "Map has wrong layout");

} // namespace shim
} // namespace v8
