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
        result(int e) : _value(std::nullopt), err(e)
        {   }

        result(const T& val) : _value(val), err(0)
        {   }

        auto good() const { return _value.has_value(); }
        auto error() const { return err; }

        auto& value() const { return _value.value(); }
    };

    enum class access
    {
        ro, wo
    };
}