/* This file is part of Plast.

   Plast is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Plast is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Plast.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef Host_h
#define Host_h

struct Host
{
    String address;
    uint16_t port;
    String friendlyName;
    String toString() const { return String::format<128>("%s:%d", friendlyName.isEmpty() ? address.constData() : friendlyName.constData(), port); }
    bool operator==(const Host &other) const { return address == other.address && port == other.port; }
    bool operator<(const Host &other) const
    {
        const int cmp = address.compare(other.address);
        return cmp < 0 || (!cmp && port < other.port);
    }
};

namespace std
{
template <> struct hash<Host> : public unary_function<Host, size_t>
{
    size_t operator()(const Host& value) const
    {
        std::hash<String> h1;
        std::hash<uint16_t> h2;
        return h1(value.address) | h2(value.port);
    }
};
}

inline Serializer &operator<<(Serializer &serializer, const Host &host)
{
    serializer << host.address << host.port << host.friendlyName;
    return serializer;
}

inline Deserializer &operator>>(Deserializer &deserializer, Host &host)
{
    deserializer >> host.address >> host.port >> host.friendlyName;
    return deserializer;
}

#endif
