// See json.h.
#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace gta2net
{

const Json& Json::Null()
{
    static const Json null;
    return null;
}

const std::string& Json::Empty()
{
    static const std::string empty;
    return empty;
}

bool Json::IsInteger() const
{
    return mType == kNumber && isfinite(mNumber) && floor(mNumber) == mNumber;
}

std::string Json::ToText() const
{
    switch (mType)
    {
        case kString:
            return mString;
        case kNumber:
        case kBool:
        case kNull:
            return Dump();
        default:
            return Dump();
    }
}

bool Json::Truthy() const
{
    switch (mType)
    {
        case kNull:
            return false;
        case kBool:
            return mBool;
        case kNumber:
            return mNumber != 0 && !isnan(mNumber);
        case kString:
            return !mString.empty();
        default:
            return true;
    }
}

Json& Json::Push(Json v)
{
    if (mType != kArray)
    {
        *this = Array();
    }
    mItems.push_back(std::move(v));
    return mItems.back();
}

const Json& Json::operator[](const std::string& key) const
{
    if (mType == kObject)
    {
        for (const auto& f : mFields)
        {
            if (f.first == key)
            {
                return f.second;
            }
        }
    }
    return Null();
}

bool Json::Has(const std::string& key) const
{
    if (mType == kObject)
    {
        for (const auto& f : mFields)
        {
            if (f.first == key)
            {
                return true;
            }
        }
    }
    return false;
}

Json& Json::Set(const std::string& key, Json v)
{
    if (mType != kObject)
    {
        *this = Object();
    }
    for (auto& f : mFields)
    {
        if (f.first == key)
        {
            f.second = std::move(v);
            return f.second;
        }
    }
    mFields.emplace_back(key, std::move(v));
    return mFields.back().second;
}

void Json::Erase(const std::string& key)
{
    for (size_t i = 0; i < mFields.size(); ++i)
    {
        if (mFields[i].first == key)
        {
            mFields.erase(mFields.begin() + static_cast<long>(i));
            return;
        }
    }
}

bool Json::operator==(const Json& o) const
{
    if (mType != o.mType)
    {
        return false;
    }
    switch (mType)
    {
        case kNull:
            return true;
        case kBool:
            return mBool == o.mBool;
        case kNumber:
            return mNumber == o.mNumber;
        case kString:
            return mString == o.mString;
        case kArray:
            return mItems == o.mItems;
        case kObject:
            if (mFields.size() != o.mFields.size())
            {
                return false;
            }
            for (const auto& f : mFields)
            {
                if (!o.Has(f.first) || o[f.first] != f.second)
                {
                    return false;
                }
            }
            return true;
    }
    return false;
}

static void DumpString(const std::string& s, std::string& out)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c); // UTF-8 passes through, as JSON.stringify keeps it
                }
        }
    }
    out += '"';
}

void Json::DumpTo(std::string& out) const
{
    switch (mType)
    {
        case kNull:
            out += "null";
            break;
        case kBool:
            out += mBool ? "true" : "false";
            break;
        case kNumber:
        {
            char buf[32];
            if (!isfinite(mNumber))
            {
                out += "null"; // as JSON.stringify
            }
            else if (IsInteger() && fabs(mNumber) < 1e15)
            {
                snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(mNumber));
                out += buf;
            }
            else
            {
                snprintf(buf, sizeof(buf), "%.17g", mNumber);
                out += buf;
            }
            break;
        }
        case kString:
            DumpString(mString, out);
            break;
        case kArray:
            out += '[';
            for (size_t i = 0; i < mItems.size(); ++i)
            {
                if (i)
                {
                    out += ',';
                }
                mItems[i].DumpTo(out);
            }
            out += ']';
            break;
        case kObject:
            out += '{';
            for (size_t i = 0; i < mFields.size(); ++i)
            {
                if (i)
                {
                    out += ',';
                }
                DumpString(mFields[i].first, out);
                out += ':';
                mFields[i].second.DumpTo(out);
            }
            out += '}';
            break;
    }
}

std::string Json::Dump() const
{
    std::string out;
    DumpTo(out);
    return out;
}

namespace
{
struct Parser
{
    const std::string& s;
    size_t i = 0;
    int depth = 0;

    void Space()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        {
            ++i;
        }
    }

    bool Literal(const char* word)
    {
        size_t n = 0;
        while (word[n])
        {
            if (i + n >= s.size() || s[i + n] != word[n])
            {
                return false;
            }
            ++n;
        }
        i += n;
        return true;
    }

    static void Utf8(unsigned cp, std::string& out)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(unsigned* out)
    {
        if (i + 4 > s.size())
        {
            return false;
        }
        unsigned v = 0;
        for (int k = 0; k < 4; ++k)
        {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= static_cast<unsigned>(c - 'A' + 10);
            else
                return false;
        }
        *out = v;
        return true;
    }

    bool String(std::string* out)
    {
        if (i >= s.size() || s[i] != '"')
        {
            return false;
        }
        ++i;
        while (i < s.size())
        {
            const char c = s[i++];
            if (c == '"')
            {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20)
            {
                return false;
            }
            if (c != '\\')
            {
                *out += c;
                continue;
            }
            if (i >= s.size())
            {
                return false;
            }
            const char e = s[i++];
            switch (e)
            {
                case '"':
                case '\\':
                case '/':
                    *out += e;
                    break;
                case 'n':
                    *out += '\n';
                    break;
                case 'r':
                    *out += '\r';
                    break;
                case 't':
                    *out += '\t';
                    break;
                case 'b':
                    *out += '\b';
                    break;
                case 'f':
                    *out += '\f';
                    break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!Hex4(&cp))
                    {
                        return false;
                    }
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u')
                    {
                        i += 2;
                        unsigned lo = 0;
                        if (!Hex4(&lo) || lo < 0xDC00 || lo >= 0xE000)
                        {
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    Utf8(cp, *out);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool Value(Json* out)
    {
        if (++depth > 64)
        {
            return false;
        }
        Space();
        if (i >= s.size())
        {
            return false;
        }
        const char c = s[i];
        bool ok = true;
        if (c == '{')
        {
            ++i;
            *out = Json::Object();
            Space();
            if (i < s.size() && s[i] == '}')
            {
                ++i;
            }
            else
            {
                for (;;)
                {
                    Space();
                    std::string key;
                    if (!String(&key))
                    {
                        return false;
                    }
                    Space();
                    if (i >= s.size() || s[i++] != ':')
                    {
                        return false;
                    }
                    Json v;
                    if (!Value(&v))
                    {
                        return false;
                    }
                    out->Set(key, std::move(v));
                    Space();
                    if (i < s.size() && s[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}')
                    {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        }
        else if (c == '[')
        {
            ++i;
            *out = Json::Array();
            Space();
            if (i < s.size() && s[i] == ']')
            {
                ++i;
            }
            else
            {
                for (;;)
                {
                    Json v;
                    if (!Value(&v))
                    {
                        return false;
                    }
                    out->Push(std::move(v));
                    Space();
                    if (i < s.size() && s[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']')
                    {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        }
        else if (c == '"')
        {
            std::string str;
            ok = String(&str);
            *out = Json(std::move(str));
        }
        else if (Literal("true"))
        {
            *out = Json(true);
        }
        else if (Literal("false"))
        {
            *out = Json(false);
        }
        else if (Literal("null"))
        {
            *out = Json();
        }
        else if (c == '-' || (c >= '0' && c <= '9'))
        {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            const double v = strtod(start, &end);
            if (end == start)
            {
                return false;
            }
            i += static_cast<size_t>(end - start);
            *out = Json(v);
        }
        else
        {
            ok = false;
        }
        --depth;
        return ok;
    }
};
} // namespace

bool Json::Parse(const std::string& text, Json* out)
{
    Parser p{text};
    Json v;
    if (!p.Value(&v))
    {
        *out = Json();
        return false;
    }
    p.Space();
    if (p.i != text.size())
    {
        *out = Json();
        return false;
    }
    *out = std::move(v);
    return true;
}

} // namespace gta2net
