#pragma once

#include <iostream>
#include <optional>
#include <array>
#include <any>
#include <memory>
#include <cassert>
#include <vector>
#include <sstream>

namespace pio::io
{
    /**
     * @brief Represents a single async request for a given type.
     * 
     * @tparam _Type The @ref Type of the object
     */
    template<typename _Type>
    struct request 
    { 
        const static nc_type type = _Type::nc;

        using integral_type = typename _Type::integral_type;

        std::shared_ptr<integral_type> data;
        const uint32_t count;

        /**
         * @brief Construct a new request object and allocates data for _count objects.
         * @param _count The amount of data objects contained in the request.
         */
        request(uint32_t _count) :
            data((integral_type*)std::calloc(_count, sizeof(integral_type)), [](void* p) { if (p) std::free(p); }),
            count(_count)
        {   }
        
        /**
         * @brief Helper function for getting a list of strings.
         */
        template<typename = std::enable_if<std::is_same_v<_Type, io::type<NC_CHAR>>>>
        std::vector<std::string>
        get_strings() const
        {
            std::vector<std::string> ret;
            
            std::stringstream ss;
            for (uint32_t i = 0; i < count; i++)
            {
                if (value()[i] == '\0') 
                {
                    if (ss.str().length())  
                        ret.push_back(ss.str());
                    ss.str(std::string(""));
                    continue;
                }

                ss << value()[i];
            }

            return ret;
        }

        integral_type* value() { return data.get(); }
        const integral_type* value() const { return data.get(); }
    };

    /**
     * @brief Represents a collection of request objects.
     * @tparam _Types Types corresponding to the different requests
     */
    template<typename... _Types>
    struct request_array
    {
        std::shared_ptr<int> ids;
        std::array<std::any, sizeof...(_Types)> requests;

        /**
         * @brief Construct a new request array object
         * 
         * @param counts Counts for each request
         * @param req_count Optional parameter to specify different amount of requests (that is if there are multiple requests per request object)
         */
        request_array(
            const std::array<uint32_t, sizeof...(_Types)>& counts,
            std::optional<uint32_t> req_count = std::nullopt
            ) :
            _req_count(
                req_count.has_value()?
                req_count.value() : sizeof...(_Types)
            ),
            ids((int*)std::calloc(_req_count, sizeof(int)), [](void* p) { if (p) std::free(p); })
        {
            if (req_count.has_value()) assert(req_count.value() >= sizeof...(_Types));
            emplace<_Types...>(0, counts);
        }

        uint32_t request_count() const { return _req_count; }

        template<uint32_t index>
        auto&
        get()
        {
            using nth_type = std::tuple_element_t<index, std::tuple<_Types...>>;
            return *std::any_cast<request<nth_type>>(&requests[index]);
        }

        template<uint32_t index>
        const auto&
        get() const
        {
            using nth_type = std::tuple_element_t<index, std::tuple<_Types...>>;
            return *std::any_cast<request<nth_type>>(&requests[index]);
        }

    private:
        template<typename _Type, typename... _Rest>
        void emplace(int index, const std::array<uint32_t, sizeof...(_Types)>& counts)
        {
            using type = request<_Type>;
            requests[index] = std::make_any<type>(counts[index]);
            if constexpr (sizeof...(_Rest)) emplace<_Rest...>(index + 1, counts);
        }

        const uint32_t _req_count;
    };

    // change int errors to err::code
    // we should only allocate data if io is read, so 
    //   maybe add a new tempalte type that is io::access
    template<typename... _Types>
    class promise
    {
        std::optional<request_array<_Types...>> _requests;
        std::optional<int> _error;
        const int _handle;

    public:
        promise(int error) :
            _error(error),
            _handle(0)
        {   }

        promise(
                int handle, 
                const std::array<uint32_t, sizeof...(_Types)>& counts, 
                std::optional<uint32_t> req_count = std::nullopt
            ) :
            _requests(std::make_optional<request_array<_Types...>>(counts, req_count)),
            _handle(handle)
        {   }

        template<uint32_t _Index>
        const auto&
        get() const 
        {
            assert(_requests.has_value());
            return _requests.value().template get<_Index>();
        }
        
        template<uint32_t _Index>
        auto&
        get() 
        {
            assert(_requests.has_value());
            return _requests.value().template get<_Index>();
        }

        std::vector<std::string> 
        wait_for_completion() const
        {
            assert(_requests.has_value());
            const auto req_count = _requests.value().request_count();
            std::vector<int> status(req_count); 
            auto* requests = const_cast<int*>(_requests.value().ids.get());

            std::cout << "starting\n";
            const auto error = ncmpi_wait_all(_handle, req_count, requests, status.data());
            std::cout << "done\n";
            if (error == NC_NOERR)
            {
                std::vector<std::string> ret;
                ret.reserve(req_count);
                for (const auto& i : status) ret.push_back(std::string(ncmpi_strerror(i)));
                return ret;
            }
            std::cout << "ERROR!!!" << error << "\n";
            return { };
        }

        int* requests() { return _requests.value().ids.get(); }
        const int* requests() const { return _requests.value().ids.get(); }

        uint32_t request_count() const { return sizeof...(_Types); }

        bool good() const
        {
            return (!_error.has_value());
        }

        auto error() const { return (good()?0:_error.value()); }
    };
} 
