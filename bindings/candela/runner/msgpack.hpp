// MessagePack straight to and from engine values, for the bytes the Candela
// host and the engine pass each other (host/src/wire.rs is the other side).
//
// Pack writes values into a byte buffer without building a document first.
// Msg reads one: a view of a value inside the bytes, with the lookups the
// codecs need (a field by name, an element by index, a number with a
// fallback). Neither allocates beyond the buffer Pack writes into.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace drift::candela {

class Pack {
public:
    explicit Pack(std::vector<uint8_t>& out) : out_(out) {}

    void nil() { out_.push_back(0xc0); }
    void val(bool b) { out_.push_back(b ? 0xc3 : 0xc2); }
    void val(float f) {
        out_.push_back(0xca);
        uint32_t u;
        std::memcpy(&u, &f, 4);
        be(u, 4);
    }
    void val(double d) {
        // A double that is a float exactly is written as one, as nlohmann
        // did, so values read back are the same either way.
        float f = static_cast<float>(d);
        if (static_cast<double>(f) == d) return val(f);
        out_.push_back(0xcb);
        uint64_t u;
        std::memcpy(&u, &d, 8);
        be(u, 8);
    }
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    void val(T i) {
        if constexpr (std::is_signed_v<T>) sint(static_cast<int64_t>(i));
        else uint(static_cast<uint64_t>(i));
    }
    void val(std::string_view s) {
        size_t n = s.size();
        if (n <= 31) {
            out_.push_back(static_cast<uint8_t>(0xa0 | n));
        } else if (n <= 0xff) {
            out_.push_back(0xd9);
            out_.push_back(static_cast<uint8_t>(n));
        } else {
            out_.push_back(0xdb);
            be(n, 4);
        }
        out_.insert(out_.end(), s.begin(), s.end());
    }
    void val(const char* s) { val(std::string_view(s)); }
    void val(const std::string& s) { val(std::string_view(s)); }
    // A value already written as MessagePack.
    void raw(std::string_view bytes) { out_.insert(out_.end(), bytes.begin(), bytes.end()); }

    void array(size_t n) {
        if (n <= 15) {
            out_.push_back(static_cast<uint8_t>(0x90 | n));
        } else {
            out_.push_back(0xdd);
            be(n, 4);
        }
    }
    // A list of numbers or strings.
    template <typename T> void list(const std::vector<T>& items) {
        array(items.size());
        for (const T& x : items) val(x);
    }

    // A map whose entries `body` writes with key() then a value; the count
    // is filled in once it is done.
    template <typename F> void object(F&& body) {
        size_t at = out_.size();
        out_.push_back(0xde);
        out_.push_back(0);
        out_.push_back(0);
        counts_.push_back(0);
        body();
        uint32_t n = counts_.back();
        counts_.pop_back();
        out_[at + 1] = static_cast<uint8_t>(n >> 8);
        out_[at + 2] = static_cast<uint8_t>(n);
    }
    void key(std::string_view k) {
        if (!counts_.empty()) ++counts_.back();
        val(k);
    }
    template <typename T> void kv(std::string_view k, const T& v) {
        key(k);
        val(v);
    }
    // `{"id": id}`, the shape of every handle and of Entity.
    void id(std::string_view k, uint64_t id) {
        key(k);
        object([&] { kv("id", id); });
    }

private:
    void sint(int64_t i) {
        if (i >= 0) return uint(static_cast<uint64_t>(i));
        if (i >= -32) {
            out_.push_back(static_cast<uint8_t>(static_cast<int8_t>(i)));
        } else {
            out_.push_back(0xd3);
            be(static_cast<uint64_t>(i), 8);
        }
    }
    void uint(uint64_t u) {
        if (u <= 0x7f) {
            out_.push_back(static_cast<uint8_t>(u));
        } else {
            out_.push_back(0xcf);
            be(u, 8);
        }
    }
    void be(uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    std::vector<uint8_t>& out_;
    std::vector<uint32_t> counts_;
};

// A value inside MessagePack bytes. A missing value (a field a map does not
// have, an index past the end) is a null view, and asking a null view for
// anything gives the fallback, so lookups chain without checks.
class Msg {
public:
    Msg() = default;
    Msg(const uint8_t* at, const uint8_t* end) : at_(at), end_(end) {}

    bool valid() const { return at_ && at_ < end_; }
    bool is_null() const { return !valid() || *at_ == 0xc0; }
    bool is_object() const { return valid() && (map_len() != npos); }
    bool is_array() const { return valid() && (array_len() != npos); }
    bool is_string() const { return valid() && str_len(nullptr) != npos; }
    bool is_boolean() const { return valid() && (*at_ == 0xc2 || *at_ == 0xc3); }
    bool is_number() const {
        if (!valid()) return false;
        uint8_t b = *at_;
        return b <= 0x7f || b >= 0xe0 || (b >= 0xca && b <= 0xd3);
    }
    bool is_number_integer() const { return is_number() && *at_ != 0xca && *at_ != 0xcb; }

    // The entries of a map or the elements of an array.
    size_t size() const {
        size_t n = map_len();
        if (n != npos) return n;
        n = array_len();
        return n != npos ? n : 0;
    }

    // A field of a map, by name.
    Msg operator[](std::string_view key) const {
        size_t n = map_len();
        if (n == npos) return {};
        const uint8_t* p = body();
        for (size_t i = 0; i < n && p; ++i) {
            Msg k(p, end_);
            const uint8_t* v = k.skip();
            if (!v) return {};
            std::string_view ks;
            if (k.str(ks) && ks == key) return {v, end_};
            p = Msg(v, end_).skip();
        }
        return {};
    }
    Msg operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    Msg operator[](const std::string& key) const { return (*this)[std::string_view(key)]; }
    // An element of an array.
    Msg operator[](size_t index) const {
        size_t n = array_len();
        if (n == npos || index >= n) return {};
        const uint8_t* p = body();
        for (size_t i = 0; i < index && p; ++i) p = Msg(p, end_).skip();
        return p ? Msg(p, end_) : Msg{};
    }
    Msg operator[](int index) const { return (*this)[static_cast<size_t>(index)]; }
    bool contains(std::string_view key) const { return (*this)[key].valid(); }

    template <typename T> T get() const {
        T out{};
        read(out);
        return out;
    }
    // A field read as T, or `fallback` when the map lacks it or it is not
    // a T.
    template <typename T> T value(std::string_view key, T fallback) const {
        Msg v = (*this)[key];
        T out = fallback;
        if (v.valid() && !v.read(out)) return fallback;
        return out;
    }
    std::string value(std::string_view key, const char* fallback) const { return value(key, std::string(fallback)); }

    // Calls fn(element) for each element of an array.
    template <typename F> void each(F&& fn) const {
        size_t n = array_len();
        if (n == npos) return;
        const uint8_t* p = body();
        for (size_t i = 0; i < n && p; ++i) {
            fn(Msg(p, end_));
            p = Msg(p, end_).skip();
        }
    }
    // Calls fn(key, value) for each entry of a map.
    template <typename F> void items(F&& fn) const {
        size_t n = map_len();
        if (n == npos) return;
        const uint8_t* p = body();
        for (size_t i = 0; i < n && p; ++i) {
            Msg k(p, end_);
            const uint8_t* v = k.skip();
            if (!v) return;
            std::string_view ks;
            k.str(ks);
            fn(ks, Msg(v, end_));
            p = Msg(v, end_).skip();
        }
    }

    // The bytes of this value.
    std::string_view bytes() const {
        const uint8_t* e = skip();
        if (!e) return {};
        return {reinterpret_cast<const char*>(at_), static_cast<size_t>(e - at_)};
    }

    // Where the next value starts, or null when the bytes are cut short.
    const uint8_t* skip() const {
        if (!valid()) return nullptr;
        uint8_t b = *at_;
        const uint8_t* p = at_ + 1;
        auto need = [&](size_t n) -> const uint8_t* { return size_t(end_ - p) >= n ? p + n : nullptr; };
        if (b <= 0x7f || b >= 0xe0 || b == 0xc0 || b == 0xc2 || b == 0xc3) return p;
        if ((b & 0xe0) == 0xa0) return need(b & 0x1f);
        if ((b & 0xf0) == 0x80 || (b & 0xf0) == 0x90 || b == 0xdc || b == 0xdd || b == 0xde || b == 0xdf) {
            bool map = (b & 0xf0) == 0x80 || b == 0xde || b == 0xdf;
            size_t n = map ? map_len() : array_len();
            const uint8_t* q = body();
            if (!q) return nullptr;
            for (size_t i = 0; i < n * (map ? 2 : 1); ++i) {
                q = Msg(q, end_).skip();
                if (!q) return nullptr;
            }
            return q;
        }
        switch (b) {
        case 0xcc: case 0xd0: return need(1);
        case 0xcd: case 0xd1: return need(2);
        case 0xca: case 0xce: case 0xd2: return need(4);
        case 0xcb: case 0xcf: case 0xd3: return need(8);
        case 0xd9: case 0xc4: { const uint8_t* q = need(1); return q ? need(1 + p[0]) : nullptr; }
        case 0xda: case 0xc5: { const uint8_t* q = need(2); return q ? need(2 + (size_t(p[0]) << 8 | p[1])) : nullptr; }
        case 0xdb: case 0xc6: {
            const uint8_t* q = need(4);
            return q ? need(4 + (size_t(p[0]) << 24 | size_t(p[1]) << 16 | size_t(p[2]) << 8 | p[3])) : nullptr;
        }
        default: return nullptr;
        }
    }

private:
    static constexpr size_t npos = static_cast<size_t>(-1);

    uint64_t be(const uint8_t* p, int n) const {
        uint64_t v = 0;
        for (int i = 0; i < n; ++i) v = v << 8 | p[i];
        return v;
    }
    bool has(size_t n) const { return size_t(end_ - at_) >= n + 1; }

    size_t map_len() const {
        if (!valid()) return npos;
        uint8_t b = *at_;
        if ((b & 0xf0) == 0x80) return b & 0x0f;
        if (b == 0xde && has(2)) return be(at_ + 1, 2);
        if (b == 0xdf && has(4)) return be(at_ + 1, 4);
        return npos;
    }
    size_t array_len() const {
        if (!valid()) return npos;
        uint8_t b = *at_;
        if ((b & 0xf0) == 0x90) return b & 0x0f;
        if (b == 0xdc && has(2)) return be(at_ + 1, 2);
        if (b == 0xdd && has(4)) return be(at_ + 1, 4);
        return npos;
    }
    // Where the entries of a map or array start.
    const uint8_t* body() const {
        uint8_t b = *at_;
        const uint8_t* p = at_ + ((b == 0xde || b == 0xdc) ? 3 : (b == 0xdf || b == 0xdd) ? 5 : 1);
        return p <= end_ ? p : nullptr;
    }
    size_t str_len(const uint8_t** start) const {
        uint8_t b = *at_;
        size_t n = npos, head = 0;
        if ((b & 0xe0) == 0xa0) { n = b & 0x1f; head = 1; }
        else if (b == 0xd9 && has(1)) { n = at_[1]; head = 2; }
        else if (b == 0xda && has(2)) { n = be(at_ + 1, 2); head = 3; }
        else if (b == 0xdb && has(4)) { n = be(at_ + 1, 4); head = 5; }
        if (n == npos || size_t(end_ - at_) < head + n) return npos;
        if (start) *start = at_ + head;
        return n;
    }
    bool str(std::string_view& out) const {
        if (!valid()) return false;
        const uint8_t* s = nullptr;
        size_t n = str_len(&s);
        if (n == npos) return false;
        out = {reinterpret_cast<const char*>(s), n};
        return true;
    }
    bool number(double& d, int64_t& i, bool& integer) const {
        if (!valid()) return false;
        uint8_t b = *at_;
        integer = true;
        if (b <= 0x7f) { i = b; }
        else if (b >= 0xe0) { i = static_cast<int8_t>(b); }
        else if (b == 0xca && has(4)) {
            uint32_t u = static_cast<uint32_t>(be(at_ + 1, 4));
            float f;
            std::memcpy(&f, &u, 4);
            d = f;
            integer = false;
        } else if (b == 0xcb && has(8)) {
            uint64_t u = be(at_ + 1, 8);
            std::memcpy(&d, &u, 8);
            integer = false;
        }
        else if (b == 0xcc && has(1)) i = static_cast<int64_t>(be(at_ + 1, 1));
        else if (b == 0xcd && has(2)) i = static_cast<int64_t>(be(at_ + 1, 2));
        else if (b == 0xce && has(4)) i = static_cast<int64_t>(be(at_ + 1, 4));
        else if (b == 0xcf && has(8)) i = static_cast<int64_t>(be(at_ + 1, 8));
        else if (b == 0xd0 && has(1)) i = static_cast<int8_t>(at_[1]);
        else if (b == 0xd1 && has(2)) i = static_cast<int16_t>(be(at_ + 1, 2));
        else if (b == 0xd2 && has(4)) i = static_cast<int32_t>(be(at_ + 1, 4));
        else if (b == 0xd3 && has(8)) i = static_cast<int64_t>(be(at_ + 1, 8));
        else return false;
        if (integer) d = static_cast<double>(i);
        return true;
    }

    // As nlohmann's get<T>: a number converts to any arithmetic type, a
    // bool only to bool, text only to a string.
    template <typename T> bool read(T& out) const {
        if constexpr (std::is_same_v<T, bool>) {
            if (!is_boolean()) return false;
            out = *at_ == 0xc3;
            return true;
        } else if constexpr (std::is_arithmetic_v<T>) {
            double d = 0;
            int64_t i = 0;
            bool integer = false;
            if (!number(d, i, integer)) return false;
            if constexpr (std::is_floating_point_v<T>) out = static_cast<T>(d);
            else out = integer ? static_cast<T>(i) : static_cast<T>(d);
            return true;
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::string_view s;
            if (!str(s)) return false;
            out.assign(s.data(), s.size());
            return true;
        } else if constexpr (std::is_same_v<T, Msg>) {
            out = *this;
            return true;
        } else {
            static_assert(sizeof(T) == 0, "Msg reads numbers, bools and strings");
            return false;
        }
    }

    const uint8_t* at_ = nullptr;
    const uint8_t* end_ = nullptr;
};

} // namespace drift::candela
