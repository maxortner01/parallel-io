#pragma once

#include <optional>

namespace pio::io
{
    template<typename T>
    class result
    {
        int err;
        std::optional<T> _value;

    public:
        result(int e);
        result(const T& val);

        auto  good()  const;
        auto  error() const;
        auto& value() const;
        operator bool() const { return good(); }
    };
}

namespace pio::io
{
    template<typename T>
    result<T>::result(int e) : _value(std::nullopt), err(e)
    {   }

    template<typename T>
    result<T>::result(const T& val) : _value(val), err(0)
    {   }

    template<typename T>
    auto result<T>::good() const 
    { return _value.has_value(); }

    template<typename T>
    auto result<T>::error() const
    { return err; }

    template<typename T>
    auto& result<T>::value() const
    { return _value.value(); }
}