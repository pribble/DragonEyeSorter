// json_io.hpp — typed nlohmann::json accessors for the config / calib / flat-field / HMI loaders. Header-only.
// Missing or JSON-null keys are silent (false / untouched); present wrong-type keys keep nlohmann's exception
// (loaders report "<path>: <what()>"). Keys are const char* literals.
#pragma once
#include <cstddef>
#include <string>
#include "geom.hpp"
#include "json.hpp"

namespace des {
namespace ji {

// Present and not null -> out = j[key]. Absent / null -> false; wrong type throws like j.at(key).get<T>().
template <class T> bool get(const nlohmann::json &j, const char *key, T &out)
{
    if (!j.is_object()) return false;
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return false;
    out = it->get<T>();
    return true;
}
// [a, b] -> (a, b) (Vec2 x/y pairs); absent / null / malformed pair -> false, both untouched.
template <class A, class B> bool get(const nlohmann::json &j, const char *key, A &a, B &b)
{
    if (!j.is_object()) return false;
    const auto it = j.find(key);
    if (it == j.end() || it->is_null() || !it->is_array() || it->size() != 2) return false;
    a = it->at(0).get<A>();
    b = it->at(1).get<B>();
    return true;
}
inline bool vec2(const nlohmann::json &j, const char *key, Vec2 &out) { return get(j, key, out.x, out.y); }
// 9 numbers (row-major 3x3) -> Homography; false when absent, wrong length / element type throws.
inline bool homography(const nlohmann::json &j, const char *key, Homography &out)
{
    if (!j.is_object()) return false;
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return false;
    if (!it->is_array() || it->size() != 9)
        throw nlohmann::json::type_error::create(302, std::string(key) + ": need an array of 9 numbers (row-major 3x3)", &*it);
    for (int i = 0; i < 9; i++) out.h[size_t(i)] = it->at(size_t(i)).get<double>();
    return true;
}
// degrees in the file -> radians in the struct; false when absent / null.
inline bool deg(const nlohmann::json &j, const char *key, float &rad)
{
    float d = 0;
    if (!get(j, key, d)) return false;
    rad = d * kPi / 180.f;
    return true;
}
// Section object member, or nullptr when absent / not an object.
inline const nlohmann::json *sec(const nlohmann::json &j, const char *key)
{
    if (!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return (it == j.end() || !it->is_object()) ? nullptr : &*it;
}
// Alternating const char* key / reference pairs, e.g. gets(c, "scale", l.scale, "thresh", l.thresh);
// absent keys are ignored, a present key with the wrong type throws.
inline void gets(const nlohmann::json &) {}
template <class T, class... Rest> void gets(const nlohmann::json &j, const char *key, T &out, Rest &&...rest)
{
    static_assert(sizeof...(Rest) % 2 == 0, "gets(): expected alternating const char* key / value reference pairs");
    get(j, key, out);
    gets(j, rest...);
}

}  // namespace ji
}  // namespace des
