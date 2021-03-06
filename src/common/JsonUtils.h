#ifndef JSONUTILS_H
#define JSONUTILS_H

#include <json.hpp>

class JsonObject
{
public:
    JsonObject()
    {
    }

    nlohmann::json& object() { return j; }

    bool hasKey() const { return !k.isEmpty(); }
    void setKey(const String& key) { k = key; }

    template<typename T>
    void setValue(const T& t)
    {
        assert(hasKey());
        j[k] = t;
        k.clear();
    }

    String dump() const
    {
        return j.dump();
    }

private:
    nlohmann::json j;
    String k;
};

template <typename T>
T safe_get(const nlohmann::json &j, bool *ok = 0, const T &defaultValue = T())
{
    try {
        const T ret = j.get<T>();
        if (ok)
            *ok = true;
        return ret;
    } catch(const std::exception &e) {
        if (ok)
            *ok = false;
        return defaultValue;
    }
}

template <typename T>
T safe_get(const nlohmann::json &parent, const String &key, bool *ok = 0, const T &defaultValue = T())
{
    if (parent.is_object())
        return safe_get<T>(parent[key], ok, defaultValue);
    if (ok)
        *ok = false;
    return defaultValue;
}

template<typename T>
inline JsonObject&& operator<<(JsonObject&& o, const T& t)
{
    o.setValue<T>(t);
    return std::forward<JsonObject>(o);
}

inline JsonObject&& operator<<(JsonObject&& o, const char* s)
{
    if (!o.hasKey())
        o.setKey(s);
    else
        o.setValue(s);
    return std::forward<JsonObject>(o);
}

inline JsonObject&& operator<<(JsonObject&& o, const String& s)
{
    return std::forward<JsonObject>(operator<<(std::forward<JsonObject>(o), s.constData()));
}

#endif
