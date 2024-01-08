#pragma once

#include <iostream>
#include <cassert>
#include <optional>

namespace pio::io
{
    namespace detail
    {

    template<typename T, typename E = int>
    struct base_result
    {
        base_result() : _error(std::nullopt)
        {   }
        
        base_result(const E& e) : _error(e)
        {   }

        base_result(const base_result&) = delete;

        virtual ~base_result() = default;

        bool     good()  const { return !_error.has_value(); }
        const E& error() const { return _error.value(); }

        operator bool() const { return good(); }

    private:
        std::optional<E> _error;
    };

    }

    template<typename T, typename E = int>
    struct result : detail::base_result<T, E>
    {
        using detail::base_result<T, E>::base_result;

        result(T&& val) : _value(val)
        {   }

        ~result() = default;

        T&       value()       { return _value.value(); }
        const T& value() const { return _value.value(); }

    private:
        std::optional<T> _value;
    };

    template<typename E>
    struct result<void, E> : detail::base_result<void, E>
    {   
        using detail::base_result<void, E>::base_result;

        result()  = default;
        ~result() = default;
    };
}