#pragma once

#include <iostream>
#include <cassert>
#include <optional>

namespace pio::io
{
    template<typename T, typename E = int>
    class result
    {
        std::optional<T> _value;
        std::optional<E> _error;

    public:
        result(const E& e);
        result(T&& val);

        result(const result&) = delete;

        ~result() = default;

        bool     good()  const;
        T&       value();
        const E& error() const;
        const T& value() const;

        operator bool() const { return good(); }
    };
}

namespace pio::io
{
    template<typename T, typename E>
    result<T, E>::result(const E& e) : 
        _value(std::nullopt), 
        _error(e)
    {   }

    template<typename T, typename E>
    result<T, E>::result(T&& val) : 
        _value(val), 
        _error(std::nullopt)
    {   }

    template<typename T, typename E>
    bool result<T, E>::good() const 
    { return _value.has_value(); }

    template<typename T, typename E>
    const E& result<T, E>::error() const
    { assert(!good()); return _error.value(); }

    template<typename T, typename E>
    T& result<T, E>::value()
    { assert(good()); return _value.value(); }
    
    template<typename T, typename E>
    const T& result<T, E>::value() const
    { assert(good()); return _value.value(); }
}