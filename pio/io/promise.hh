#pragma once

#include <vector>
#include <memory>
#include <cstring>
#include <cassert>

#include "type.hh"

namespace pio::io
{
    namespace impl
    {

    // https://stackoverflow.com/questions/54268425/enumerating-over-a-fold-expression
    template<std::size_t... inds, class F>
    constexpr void static_for_impl(std::index_sequence<inds...>, F&& f)
    {
        (f(std::integral_constant<std::size_t, inds>{}), ...);
    }

    template<std::size_t N, class F>
    constexpr void static_for(F&& f)
    {
        static_for_impl(std::make_index_sequence<N>{}, std::forward<F>(f));
    }
    
    template<int N, typename... Ts>
    using NthType = typename std::tuple_element<N, std::tuple<Ts...>>::type;

    } // namespace impl

    template<io::access _Access, typename... _Types>
    struct promise; 

    // Does not need to allocate room for data, but only keeps track of requests
    template<typename... _Types>
    struct promise<io::access::wo, _Types...>
    {

    };

    // Needs to allocate memory to store values *and* keep track of requests
    template<typename... _Types>
    struct promise<io::access::ro, _Types...> // ro
    {
        inline static constexpr std::size_t RequestCount = sizeof...(_Types);

        struct request_handler
        {
            std::array<int, RequestCount> requests;
            std::array<std::pair<std::size_t, std::shared_ptr<void>>, RequestCount> data;
        };

        template<std::size_t _Index>
        using integral_type = typename impl::NthType<_Index, _Types...>::integral_type;

        promise(int handle, const std::array<std::size_t, RequestCount>& counts) :
            _handle(handle),
            _err(0)
        {
            _handler.emplace();
            impl::static_for<RequestCount>([&](auto n) {
                constexpr std::size_t i = n;

                using _Type = impl::NthType<i, _Types...>;
                
                auto ptr = std::shared_ptr<void>(
                    std::calloc(counts[i], sizeof(typename _Type::integral_type)),
                    [](void* ptr) { std::free(ptr); }
                );

                _handler.value().data[i] = std::pair(counts[i], ptr);
            });
        }

        promise(int error) :
            _err(error),
            _handler(std::nullopt)
        {   }

        bool good() const
        {
            return _handler.has_value();
        }

        int error() const { return _err; }

        std::array<std::string, RequestCount> wait() const
        {
            assert(good());

            std::array<std::string, RequestCount> statuses;
            std::array<int, RequestCount>         statuses_int;

            auto* reqs = const_cast<int*>(&_handler.value().requests[0]);
            const auto err = ncmpi_wait(_handle, RequestCount, reqs, statuses_int.data());
            assert(err == NC_NOERR);

            impl::static_for<RequestCount>([&](auto n) {
                constexpr std::size_t i = n;
                statuses[i] = std::string(ncmpi_strerror(statuses_int[i]));
            });

            return statuses;
        }

        template<std::size_t _Index>
        std::vector<integral_type<_Index>>
        get_data() const
        {
            assert(good());
            const auto& data_ref = _handler.value().data[_Index];
            std::vector<integral_type<_Index>> data(data_ref.first);
            std::memcpy(data.data(), data_ref.second.get(), sizeof(integral_type<_Index>) * data.size());
            return data;
        }

        template<std::size_t _Index>
        integral_type<_Index>*
        data()
        {
            assert(good());
            return static_cast<integral_type<_Index>*>(
                _handler.value().data[_Index].second.get()
            );
        }

        int* requests() { return &_handler.value().requests[0]; }

    private:
        int _handle, _err;
        std::optional<request_handler> _handler;
    };
}