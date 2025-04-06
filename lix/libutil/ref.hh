#pragma once
///@file

#include <cassert>
#include <compare>
#include <memory>
#include <exception>
#include <optional>
#include <stdexcept>

namespace nix {

/**
 * A simple non-nullable reference-counted pointer. Actually a wrapper
 * around std::shared_ptr that prevents null constructions.
 */
template<typename T>
class ref
{
private:

    std::shared_ptr<T> p;

    explicit ref<T>(const std::shared_ptr<T> & p)
        : p(p)
    {
        assert(p);
    }

public:

    ref(const ref<T> & r)
        : p(r.p)
    { }

    static ref<T> unsafeFromPtr(const std::shared_ptr<T> & p)
    {
        return ref(p);
    }

    template<std::derived_from<std::enable_shared_from_this<T>> T2>
    explicit ref(T2 & r) : p(r.shared_from_this())
    {
    }

    T* operator ->() const
    {
        return &*p;
    }

    T& operator *() const
    {
        return *p;
    }

    operator std::shared_ptr<T> () const
    {
        return p;
    }

    std::shared_ptr<T> get_ptr() const
    {
        return p;
    }

    template<typename T2>
    std::optional<ref<T2>> try_cast() const
    {
        if (auto d = std::dynamic_pointer_cast<T2>(p)) {
            return ref<T2>::unsafeFromPtr(d);
        } else {
            return std::nullopt;
        }
    }

    template<typename T2>
    std::shared_ptr<T2> try_cast_shared() const
    {
        return std::dynamic_pointer_cast<T2>(p);
    }

    template<typename T2>
    operator ref<T2> () const
    {
        return ref<T2>::unsafeFromPtr((std::shared_ptr<T2>) p);
    }

    ref<T> & operator=(ref<T> const & rhs) = default;

    bool operator == (const ref<T> & other) const
    {
        return p == other.p;
    }

    bool operator != (const ref<T> & other) const
    {
        return p != other.p;
    }

    bool operator < (const ref<T> & other) const
    {
        return p < other.p;
    }

private:

    template<typename T2, typename... Args>
    friend ref<T2>
    make_ref(Args&&... args);

};

template<typename T, typename... Args>
inline ref<T>
make_ref(Args&&... args)
{
    auto p = std::make_shared<T>(std::forward<Args>(args)...);
    return ref<T>(p);
}

}
