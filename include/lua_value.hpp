#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include <string>
#include <string_view>
#include <memory>
#include <cstring>
#include <cstdint>
#include <variant>

// Forward declarations for types used in LuaValue
class LuaObject;
struct LuaCallable;
class LuaCoroutine;
struct LuaString;

// Fast C function dispatch type
typedef void (*LuaCFunctionPtr)(const void*, size_t, void*);
struct LuaCFunction {
    LuaCFunctionPtr ptr;
    bool operator==(const LuaCFunction& other) const { return ptr == other.ptr; }
};

#define LUA_C_FUNC(f) LuaCFunction{(LuaCFunctionPtr)(f)}

enum LuaTypeIndex {
	INDEX_NIL = 0,
	INDEX_BOOLEAN = 1,
	INDEX_DOUBLE = 2,
	INDEX_INTEGER = 3,
	INDEX_STRING = 4,
	INDEX_STRING_VIEW = 5,
	INDEX_OBJECT = 6,
	INDEX_FUNCTION = 7,
	INDEX_COROUTINE = 8,
	INDEX_CFUNCTION = 9
};

// NaN Boxing Constants
// scheme: [0, 0xFFF8) -> doubles, [0xFFF8, 0xFFFF] -> tagged values
constexpr uint64_t NAN_MASK = 0xFFF8000000000000ULL;
constexpr uint64_t TAG_MASK = 0xFFFF000000000000ULL;
constexpr uint64_t PAYLOAD_MASK = 0x0000FFFFFFFFFFFFULL;

enum LuaTag : uint64_t {
    TAG_NIL      = 0xFFF8ULL << 48,
    TAG_BOOLEAN  = 0xFFF9ULL << 48,
    TAG_INTEGER  = 0xFFFAULL << 48,
    TAG_STRING   = 0xFFFBULL << 48,
    TAG_OBJECT   = 0xFFFCULL << 48,
    TAG_FUNCTION = 0xFFFDULL << 48,
    TAG_CORO     = 0xFFFEULL << 48,
    TAG_CFUNC    = 0xFFFFULL << 48
};

class LuaRefCounted {
    mutable int ref_count{0};
public:
    virtual ~LuaRefCounted() = default;
    void retain() const { ++ref_count; }
    void release() const {
        if (--ref_count == 0) {
            delete this;
        }
    }
    int get_ref_count() const { return ref_count; }
};

class LuaValue {
    uint64_t data;

    inline void retain() const {
        if (data < NAN_MASK) return;
        uint64_t tag = data & TAG_MASK;
        if (tag >= TAG_STRING && tag <= TAG_CORO) {
            uint64_t ptr_val = data & PAYLOAD_MASK;
            if (tag == TAG_STRING && (ptr_val & 1ULL)) return; // Static/pooled string, no retain
            auto* ptr = reinterpret_cast<LuaRefCounted*>(ptr_val);
            if (ptr) ptr->retain();
        }
    }

    inline void release() const {
        if (data < NAN_MASK) return;
        uint64_t tag = data & TAG_MASK;
        if (tag >= TAG_STRING && tag <= TAG_CORO) {
            uint64_t ptr_val = data & PAYLOAD_MASK;
            if (tag == TAG_STRING && (ptr_val & 1ULL)) return; // Static/pooled string, no release
            auto* ptr = reinterpret_cast<LuaRefCounted*>(ptr_val);
            if (ptr) ptr->release();
        }
    }

public:
    // Constructors
    LuaValue() : data(TAG_NIL) {}
    LuaValue(std::monostate) : data(TAG_NIL) {}
    LuaValue(bool b) : data(TAG_BOOLEAN | (b ? 1 : 0)) {}
    LuaValue(double d) {
        std::memcpy(&data, &d, sizeof(double));
        if ((data & NAN_MASK) == NAN_MASK) data = NAN_MASK; // Normalize NaNs
    }
    LuaValue(long long i) {
        if (i >= -(1LL << 47) && i < (1LL << 47)) {
            data = TAG_INTEGER | (static_cast<uint64_t>(i) & PAYLOAD_MASK);
        } else {
            double d = static_cast<double>(i);
            std::memcpy(&data, &d, sizeof(double));
            if ((data & NAN_MASK) == NAN_MASK) data = NAN_MASK;
        }
    }
    
    LuaValue(LuaObject* obj);
    LuaValue(LuaCallable* func);
    LuaValue(LuaCoroutine* coro);
    LuaValue(LuaCFunction cfunc) : data(TAG_CFUNC | (reinterpret_cast<uint64_t>(cfunc.ptr) & PAYLOAD_MASK)) {}
    LuaValue(const std::string& s);
    LuaValue(std::string_view sv);
    LuaValue(const char* s) : LuaValue(std::string_view(s ? s : "")) {}

    LuaValue(const LuaValue& other) : data(other.data) { retain(); }
    LuaValue(LuaValue&& other) noexcept : data(other.data) { other.data = TAG_NIL; }
    
    ~LuaValue() { release(); }

    LuaValue& operator=(const LuaValue& other) {
        if (this != &other) {
            uint64_t old_data = data;
            data = other.data;
            retain();
            // Assign then release to handle self-assignment correctly if we weren't checking it
            uint64_t temp = data;
            data = old_data;
            release();
            data = temp;
        }
        return *this;
    }
    
    LuaValue& operator=(LuaValue&& other) noexcept {
        if (this != &other) {
            release();
            data = other.data;
            other.data = TAG_NIL;
        }
        return *this;
    }

    size_t index() const {
        if (data < NAN_MASK) return INDEX_DOUBLE;
        static constexpr uint8_t tag_to_index[8] = {
            INDEX_NIL,        // 0xFFF8
            INDEX_BOOLEAN,    // 0xFFF9
            INDEX_INTEGER,    // 0xFFFA
            INDEX_STRING,     // 0xFFFB
            INDEX_OBJECT,     // 0xFFFC
            INDEX_FUNCTION,   // 0xFFFD
            INDEX_COROUTINE,  // 0xFFFE
            INDEX_CFUNCTION   // 0xFFFF
        };
        return tag_to_index[(data >> 48) - 0xFFF8];
    }

    bool is_nil() const { return (data & TAG_MASK) == TAG_NIL; }
    
    template <typename T> T get() const;
    template <typename T> const T* get_if() const {
        // Limited implementation of get_if for variant compatibility
        return nullptr; // Real implementation would need to store the T somewhere
    }

    bool operator==(const LuaValue& other) const;
    bool operator!=(const LuaValue& other) const { return !(*this == other); }

    uint64_t raw_data() const { return data; }
    static LuaValue from_raw(uint64_t d) { LuaValue v; v.data = d; return v; }
};

struct LuaValueHash {
	using is_transparent = void;
	size_t operator()(const LuaValue& v) const;
	size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
	size_t operator()(const char* s) const { return std::hash<std::string_view>{}(s); }
};

struct LuaValueEq {
    using is_transparent = void;
    bool operator()(const LuaValue& lhs, const LuaValue& rhs) const { return lhs == rhs; }
    bool operator()(const LuaValue& lhs, std::string_view rhs) const;
    bool operator()(std::string_view lhs, const LuaValue& rhs) const { return (*this)(rhs, lhs); }
};

#include "pool_allocator.hpp"
#include <vector>

using LuaValueVector = std::vector<LuaValue, PoolAllocator<LuaValue>>;
typedef void (*LuaCFunctionTyped)(const LuaValue*, size_t, LuaValueVector&);

// Declarations of specializations to avoid instantiation order issues
template <> bool LuaValue::get<bool>() const;
template <> double LuaValue::get<double>() const;
template <> long long LuaValue::get<long long>() const;
template <> std::string_view LuaValue::get<std::string_view>() const;
template <> std::string LuaValue::get<std::string>() const;
template <> LuaObject* LuaValue::get<LuaObject*>() const;
template <> LuaCallable* LuaValue::get<LuaCallable*>() const;
template <> LuaCoroutine* LuaValue::get<LuaCoroutine*>() const;
template <> LuaCFunction LuaValue::get<LuaCFunction>() const;

// Basic template getters for primitives (no dependencies)
template <> inline bool LuaValue::get<bool>() const { return data & 1; }

template <> inline long long LuaValue::get<long long>() const {
    if ((data & TAG_MASK) == TAG_INTEGER) {
        uint64_t payload = data & PAYLOAD_MASK;
        if (payload & (1ULL << 47)) payload |= TAG_MASK; // Sign extend
        return static_cast<long long>(payload);
    }
    return static_cast<long long>(get<double>());
}

template <> inline double LuaValue::get<double>() const {
    if ((data & TAG_MASK) == TAG_INTEGER) return static_cast<double>(get<long long>());
    double d;
    std::memcpy(&d, &data, sizeof(double));
    return d;
}

// Compatibility overloads for std::get and std::get_if
namespace std {
    template <typename T>
    inline T get(const LuaValue& v) { return v.get<T>(); }

    template <size_t I>
    inline auto get(const LuaValue& v) {
        if constexpr (I == INDEX_NIL) return LuaValue();
        else if constexpr (I == INDEX_BOOLEAN) return v.get<bool>();
        else if constexpr (I == INDEX_DOUBLE) return v.get<double>();
        else if constexpr (I == INDEX_INTEGER) return v.get<long long>();
        else if constexpr (I == INDEX_STRING) return v.get<std::string_view>();
        else if constexpr (I == INDEX_STRING_VIEW) return v.get<std::string_view>();
        else if constexpr (I == INDEX_OBJECT) return v.get<LuaObject*>();
        else if constexpr (I == INDEX_FUNCTION) return v.get<LuaCallable*>();
        else if constexpr (I == INDEX_COROUTINE) return v.get<LuaCoroutine*>();
        else if constexpr (I == INDEX_CFUNCTION) return v.get<LuaCFunction>();
    }

    template <typename T>
    inline const T* get_if(const LuaValue* v) {
        return nullptr; // Real implementation needed
    }
}

#endif // LUA_VALUE_HPP

