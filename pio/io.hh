/**
 * @file io.hh
 * @author Max Ortner (mortner@lanl.gov)
 * @brief Data structures for basic I/O.
 * @version 0.1
 * @date 2023-10-05
 * 
 * @copyright Copyright (c) 2023, Triad National Security, LLC
 */

#pragma once

#include "external.hh"

#include <sstream>
#include <vector>
#include <optional>
#include <array>
#include <any>
#include <memory>
#include <cassert>

namespace pio::error
{
    enum class code
    {
        Success
    };
}

namespace pio::io
{
    enum class access
    {
        ro = 0x01,
        wo = 0x10,
        rw = 0x11
    };

    /**
     * @brief An isomorphism of primitive data-types to MPI/NC data types.
     * 
     * @tparam The NC data type
     */
    template<nc_type T>
    struct Type
    {   };
    
    template<typename T>
    using func_ptr = int(*)(int, int, const MPI_Offset*, const MPI_Offset*, T*, int*);

    /** @copydoc Type */
    template<>
    struct Type<NC_DOUBLE> 
    { 
        const static nc_type nc = NC_DOUBLE;
        using type = double; 
        const static func_ptr<type> func;
    };

    /** @copydoc Type */
    template<>
    struct Type<NC_CHAR> 
    { 
        const static nc_type nc = NC_CHAR;
        using type = char; 
        const static func_ptr<type> func;
    };
    
    /** @copydoc Type */
    template<>
    struct Type<NC_FLOAT> 
    { 
        const static nc_type nc = NC_FLOAT;
        using type = float; 
        const static func_ptr<type> func;
    };
    
    /** @copydoc Type */
    template<>
    struct Type<NC_INT> 
    { 
        const static nc_type nc = NC_INT;
        using type = int; 
        const static func_ptr<type> func;
    };

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
    };

    /**
     * @brief Represents a single async request for a given type.
     * 
     * @tparam _Type The @ref Type of the object
     */
    template<typename _Type>
    struct request 
    { 
        const static nc_type type = _Type::nc;

        using integral_type = typename _Type::type;

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
        template<typename = std::enable_if<std::is_same_v<_Type, Type<NC_CHAR>>>>
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

            const auto error = ncmpi_wait_all(_handle, req_count, requests, status.data());
            if (error == NC_NOERR)
            {
                std::vector<std::string> ret;
                ret.reserve(req_count);
                for (const auto& i : status) ret.push_back(std::string(ncmpi_strerror(i)));
                return ret;
            }
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


namespace pio::type
{
    using Double = io::Type<NC_DOUBLE>;
    using Float = io::Type<NC_FLOAT>;
    using Char = io::Type<NC_CHAR>;
    using Int = io::Type<NC_INT>;
}

// Implementation

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