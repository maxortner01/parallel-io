#pragma once

#include <pnetcdf.h>
#include <sstream>
#include <vector>
#include <optional>
#include <array>
#include <cstring>
#include <cassert>

namespace pio::io
{
    /* Type Isomorphisms */

    template<nc_type _Type>
    struct Type { };

    template<typename T>
    using func_ptr = int(*)(int, int, const MPI_Offset*, const MPI_Offset*, T*, int*);

    template<>
    struct Type<NC_DOUBLE> 
    { 
        using type = double; 
        const static func_ptr<type> func;
    };
    
    template<>
    struct Type<NC_CHAR> 
    { 
        using type = char; 
        const static func_ptr<type> func;
    };
    
    template<>
    struct Type<NC_FLOAT> 
    { 
        using type = float; 
        const static func_ptr<type> func;
    };

    const func_ptr<Type<NC_DOUBLE>::type> Type<NC_DOUBLE>::func = &ncmpi_iget_vara_double;
    const func_ptr<Type<NC_FLOAT>::type> Type<NC_FLOAT>::func = &ncmpi_iget_vara_float;
    const func_ptr<Type<NC_CHAR>::type> Type<NC_CHAR>::func = &ncmpi_iget_vara_text;

    /* Handling Requests */

    template<nc_type _Type>
    struct request
    {
        using array_type = typename Type<_Type>::type;
        array_type* data;

        const uint32_t count;

        request(const uint32_t _count) :
            data(nullptr),
            count(_count)
        {
            data = (array_type*)std::calloc(count, sizeof(array_type));
        }

        request(const request&) = delete;

        request(request&& req) :
            data(req.data),
            count(req.count)
        {
            req.data = nullptr;
        }

        array_type& operator[](const uint32_t index)
        {
            assert(index < count);
            return data[index];
        }

        ~request()
        {
            if (data)
            {
                std::free(data);
                data = nullptr;
            }
        }
    };

    template<nc_type... _Types>
    struct TypeList 
    { 
        using types = _Types...;
    };

    template<nc_type... _Types>
    struct request_array
    {
        request_base* requests[sizeof...(_Types)];

        request_array(const std::array<uint32_t, sizeof...(_Types)> counts)
        {
            int i = 0;
            ([&]
            {
                auto* ptr = new request<_Types>(counts[i]);
                requests[i++] = ptr;
            }(), ...);
        }

        request_array(const request_array<_Types...>&) = delete;

        request_array(request_array&& req)
        {
            // Copy over request pointers and set the r-value to zero
            // so it doesn't double free
            const auto size = sizeof(request_base*) * sizeof...(_Types);
            std::memcpy(&requests[0], &req.requests[0], size);
            std::memset(&req.requests[0], 0, size);
        }

        ~request_array()
        {
            for (uint32_t i = 0; i < sizeof...(_Types); i++)
            {
                if (requests[i])
                {
                    delete requests[i];
                    requests[i] = nullptr;
                }
            }
        }

        uint32_t request_count() const { return sizeof...(_Types); }
    };

    template<typename T>
    class promise
    {
        T** _value;
        int* _req;
        
        const int _handle;
        const uint32_t _unit_count;
        const uint32_t _req_count;

    public:
        using value_type = T;
        using const_value = const T;

        promise(int error) :
            _value(nullptr),
            _req(nullptr),
            _req_count(0),
            _unit_count(0),
            _handle(-1)
        {
            
        }

        promise(int handle, const uint32_t count, const uint32_t reqs) :
            _handle(handle),
            _value(nullptr),
            _req(nullptr),
            _req_count(reqs),
            _unit_count(count)
        {
            _value = (T**)std::calloc(1, sizeof(T*));
            _value[0] = (T*)std::calloc(count, sizeof(T));
            _req   = (int*)std::calloc(reqs, sizeof(int));
        }

        promise(const promise& p) = delete;

        promise(promise&& p) :
            _value(p._value),
            _req(p._req),
            _unit_count(p._unit_count),
            _req_count(p._req_count),
            _handle(p._handle)
        {
            p._req = nullptr;
            p._value = nullptr;
        }

        ~promise()
        {
            if (_value)
            {
                if (_value[0]) 
                {
                    std::free(_value[0]);
                    _value[0] = nullptr;
                }

                std::free(_value);
                _value = nullptr;
            }

            if (_req)
            {
                std::free(_req);
                _req = nullptr;
            }
        }

        template<typename = std::enable_if<std::is_same<T, char>::value>>
        std::vector<std::string>
        get_strings() const
        {
            std::vector<std::string> ret;
            
            std::stringstream ss;
            for (uint32_t i = 0; i < count(); i++)
            {
                if (value(0)[i] == '\0') 
                {
                    if (ss.str().length())  
                        ret.push_back(ss.str());
                    ss.str(std::string(""));
                    continue;
                }

                ss << value(0)[i];
            }

            return ret;
        }

        std::vector<std::string>
        wait_for_completion() const 
        {
            std::vector<int> status(_req_count); 
            const auto error = ncmpi_wait_all(_handle, _req_count, _req, status.data());
            if (error == NC_NOERR)
            {
                std::vector<std::string> ret;
                ret.reserve(_req_count);
                for (const auto& i : status) ret.push_back(std::string(ncmpi_strerror(i)));
                return ret;
            }
            std::cout << "error: " << ncmpi_strerror(error) << "\n";
            return { };
        }

        uint32_t count() const { return _unit_count; }

        int* requests() { return _req; }

        value_type* value(const uint32_t req) { return _value[req]; }
        const_value* value(const uint32_t req) const { return _value[req]; }
    };

    /* Error handling */

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