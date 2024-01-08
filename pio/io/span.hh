#pragma once

namespace pio::util
{
    template<typename T>
    struct span
    {
        span(T* ptr, std::size_t len) :
            _ptr(ptr), _len(len)
        {   }

        T& operator[](const std::size_t& index)
        {
            // assert(index < _len)
            return _ptr[index];
        }

    private:
        T* _ptr;
        std::size_t _len;
    };
}