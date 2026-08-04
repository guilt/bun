#include "Map.h"
#include "real_v8.h"

static_assert(offsetof(v8::shim::Map, m_metaMap) == real_v8::internal::Internals::kHeapObjectMapOffset,
    "v8::Map map pointer is at wrong offset");
// kMapInstanceTypeOffset varies: 12 on 64-bit, 8 on 32-bit
// (kApiTaggedSize + kApiInt32Size). Our Map is padded to 64-bit layout
// on both architectures so external addons compiled for 64-bit see the
// correct layout. The real_v8 headers reflect the build-time arch, so
// skip this check when cross-compiling for 32-bit on a 64-bit host.
#if CPU(ADDRESS64)
static_assert(offsetof(v8::shim::Map, m_instanceType) == real_v8::internal::Internals::kMapInstanceTypeOffset,
    "v8::Map instance type is at wrong offset");
#endif

static_assert((int)v8::shim::InstanceType::String < real_v8::internal::Internals::kFirstNonstringType,
    "String instance type is not a string");
static_assert((int)v8::shim::InstanceType::Oddball == real_v8::internal::Internals::kOddballType,
    "Oddball instance type does not match V8");
static_assert((int)v8::shim::InstanceType::Object >= real_v8::internal::Internals::kFirstNonstringType,
    "Objects are strings");
static_assert((int)v8::shim::InstanceType::HeapNumber >= real_v8::internal::Internals::kFirstNonstringType,
    "HeapNumbers are strings");

static_assert(real_v8::internal::Internals::CanHaveInternalField((int)v8::shim::InstanceType::Object) == false,
    "Object instance type appears compatible with internal fields"
    "(so V8 will use direct pointer offsets instead of calling the slow path)");

namespace v8 {
namespace shim {

// TODO give these more appropriate instance types

// Prevent static initialization on startup
const Map& Map::map_map()
{
    static const Map map = Map(MapMapTag::MapMap);
    return map;
}
const Map& Map::object_map()
{
    static const Map map = Map(InstanceType::Object);
    return map;
}
const Map& Map::oddball_map()
{
    static const Map map = Map(InstanceType::Oddball);
    return map;
}
const Map& Map::string_map()
{
    static const Map map = Map(InstanceType::String);
    return map;
}
const Map& Map::heap_number_map()
{
    static const Map map = Map(InstanceType::HeapNumber);
    return map;
}

} // namespace shim
} // namespace v8
