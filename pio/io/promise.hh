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

    
    template<io::access, std::size_t>
    struct request_handler;

    template<std::size_t RequestCount>
    struct request_handler<io::access::ro, RequestCount>
    {
        std::shared_ptr<int> requests;
        std::array<std::pair<std::size_t, std::shared_ptr<void>>, RequestCount> data;
    };
    
    template<std::size_t RequestCount>
    struct request_handler<io::access::wo, RequestCount>
    {
        std::shared_ptr<int> requests;
    };

    } // namespace impl

    // RO needs to allocate memory to store values *and* keep track of requests
    // WO needs only to keep track of requests

    /// Represents a promise for the completion of a task
    template<io::access _Access, typename E, typename... _Types>
    struct promise
    {
        inline static constexpr std::size_t RequestCount = sizeof...(_Types);

        template<std::size_t _Index>
        using integral_type = typename impl::NthType<_Index, _Types...>::integral_type;

        /// Construct a promise
        /// @param handle the ID handle of the file to which this corresponds
        /// @param counts the size of the data to be retrieved for each request (a list of zeros for write-only requests)
        promise(int handle, const std::array<std::size_t, RequestCount>& counts) :
            _handle(handle),
            _error(std::nullopt)
        {
            _handler.emplace();

            _handler.value().requests = std::shared_ptr<int>(
                (int*)std::calloc(RequestCount, sizeof(int)),
                [](void* ptr) { std::free(ptr); }
            );

            // Need only allocate memory if we are reading
            if constexpr (_Access == io::access::ro)
            {
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
        }

        promise(const E& error) :
            _error(error),
            _handler(std::nullopt)
        {   }

        bool good() const
        {
            return _handler.has_value();
        }

        operator bool() const { return good(); }

        const E& error() const { assert(_error.has_value()); return _error.value(); }

        /// Block until the requests have finished
        /// @return List of status strings for each request
        std::array<std::string, RequestCount> wait() const
        {
            assert(good());

            std::array<std::string, RequestCount> statuses;
            std::array<int, RequestCount>         statuses_int;

            auto* reqs = const_cast<int*>(_handler.value().requests.get());
            const auto err = ncmpi_wait(_handle, RequestCount, reqs, statuses_int.data());
            assert(err == NC_NOERR);

            impl::static_for<RequestCount>([&](auto n) {
                constexpr std::size_t i = n;
                statuses[i] = std::string(ncmpi_strerror(statuses_int[i]));
            });

            return statuses;
        }

        /// Get the data from the given request as a vector
        /// \note \ref promise::wait() should be called before trying to access the data
        template<std::size_t _Index>
        std::vector<integral_type<_Index>>
        get_data() const
        {
            if constexpr (_Access == io::access::ro)
            {
                assert(good());
                const auto& data_ref = _handler.value().data[_Index];
                std::vector<integral_type<_Index>> data(data_ref.first);
                std::memcpy(data.data(), data_ref.second.get(), sizeof(integral_type<_Index>) * data.size());
                return data;
            }
            else
                assert(false); // need better way to handle this...
        }

        /// Get the raw data pointer for a given request
        /// \note \ref promise::wait() should be called before trying to access the data
        template<std::size_t _Index>
        integral_type<_Index>*
        data()
        {
            if constexpr (_Access == io::access::ro)
            {
                assert(good());
                return static_cast<integral_type<_Index>*>(
                    _handler.value().data[_Index].second.get()
                );
            }
            else 
                assert(false); // need better way to handle this...
        }

        int* requests() { return _handler.value().requests.get(); }

    private:
        int _handle;
        std::optional<E> _error;
        std::optional<impl::request_handler<_Access, RequestCount>> _handler;
    };
}