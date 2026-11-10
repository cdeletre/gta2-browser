// A small JSON value for the co-op lobby messages (web/net.js sends JSON text over every
// transport). Objects keep their keys in insertion order; numbers are doubles, as in
// JavaScript. Multiplayer build only (gta2-online).
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gta2net
{

class Json
{
  public:
    enum Type
    {
        kNull,
        kBool,
        kNumber,
        kString,
        kArray,
        kObject
    };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : mType(kBool), mBool(b) {}
    Json(int n) : mType(kNumber), mNumber(n) {}
    Json(double n) : mType(kNumber), mNumber(n) {}
    Json(const char* s) : mType(kString), mString(s) {}
    Json(std::string s) : mType(kString), mString(std::move(s)) {}

    static Json Array() { Json j; j.mType = kArray; return j; }
    static Json Object() { Json j; j.mType = kObject; return j; }

    Type type() const { return mType; }
    bool IsNull() const { return mType == kNull; }
    bool IsBool() const { return mType == kBool; }
    bool IsNumber() const { return mType == kNumber; }
    bool IsString() const { return mType == kString; }
    bool IsArray() const { return mType == kArray; }
    bool IsObject() const { return mType == kObject; }
    // An integer-valued number (Number.isInteger in JavaScript).
    bool IsInteger() const;

    bool Bool(bool fallback = false) const { return mType == kBool ? mBool : fallback; }
    double Number(double fallback = 0) const { return mType == kNumber ? mNumber : fallback; }
    int Int(int fallback = 0) const { return mType == kNumber ? static_cast<int>(mNumber) : fallback; }
    // The string, or fallback for anything else.
    const std::string& Str(const std::string& fallback = Empty()) const { return mType == kString ? mString : fallback; }
    // JavaScript's String(value) for the values the lobby converts (strings, numbers, null).
    std::string ToText() const;
    // JavaScript truthiness.
    bool Truthy() const;

    // Arrays
    size_t size() const { return mType == kArray ? mItems.size() : mType == kObject ? mFields.size() : 0; }
    const Json& operator[](size_t i) const { return mType == kArray && i < mItems.size() ? mItems[i] : Null(); }
    const Json& operator[](int i) const { return i < 0 ? Null() : (*this)[static_cast<size_t>(i)]; }
    Json& Push(Json v);
    Json& At(size_t i) { return mItems[i]; } // an array item, to change it in place
    const std::vector<Json>& Items() const { return mItems; }

    // Objects: a missing key reads as null; Set replaces or appends.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    bool Has(const std::string& key) const;
    Json& Set(const std::string& key, Json v);
    void Erase(const std::string& key);
    const std::vector<std::pair<std::string, Json>>& Fields() const { return mFields; }

    std::string Dump() const;
    // Parses text; returns false (and leaves *out null) on a syntax error.
    static bool Parse(const std::string& text, Json* out);

    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

    static const Json& Null();

  private:
    static const std::string& Empty();
    void DumpTo(std::string& out) const;

    Type mType = kNull;
    bool mBool = false;
    double mNumber = 0;
    std::string mString;
    std::vector<Json> mItems;
    std::vector<std::pair<std::string, Json>> mFields;
};

} // namespace gta2net
