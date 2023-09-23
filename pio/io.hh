#pragma once

#include "external.hh"

#include <sstream>
#include <vector>
#include <optional>
#include <array>
#include <any>
#include <memory>

namespace pio::err
{
    enum class code
    {
        SUCCESS
    };
}

namespace pio::io
{
    template<nc_type _Type>
    struct Type { };

    template<typename T>
    using func_ptr = int(*)(int, int, const MPI_Offset*, const MPI_Offset*, T*, int*);

    template<>
    struct Type<NC_DOUBLE> 
    { 
        const static nc_type nc = NC_DOUBLE;
        using type = double; 
        const static func_ptr<type> func;
    };
    
    template<>
    struct Type<NC_CHAR> 
    { 
        const static nc_type nc = NC_CHAR;
        using type = char; 
        const static func_ptr<type> func;
    };
    
    template<>
    struct Type<NC_FLOAT> 
    { 
        const static nc_type nc = NC_FLOAT;
        using type = float; 
        const static func_ptr<type> func;
    };

    const func_ptr<Type<NC_DOUBLE>::type> Type<NC_DOUBLE>::func = &ncmpi_iget_vara_double;
    const func_ptr<Type<NC_FLOAT>::type> Type<NC_FLOAT>::func = &ncmpi_iget_vara_float;
    const func_ptr<Type<NC_CHAR>::type> Type<NC_CHAR>::func = &ncmpi_iget_vara_text;

    template<typename _Type>
    struct request 
    { 
        const static nc_type type = _Type::nc;

        using integral_type = typename _Type::type;

        std::shared_ptr<integral_type> data;
        const uint32_t count;

        request(uint32_t _count) :
            data((integral_type*)std::calloc(_count, sizeof(integral_type))),
            count(_count)
        {   }
        
        
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

    template<typename... _Types>
    struct request_array
    {
        int ids[sizeof...(_Types)];
        std::array<std::any, sizeof...(_Types)> requests;

        request_array() = default;

        request_array(const std::array<uint32_t, sizeof...(_Types)>& counts)
        {
            emplace<_Types...>(0, counts);
        }

        uint32_t request_count() const { return sizeof...(_Types); }

        template<uint32_t index>
        auto&
        get()
        {
            using nth_type = std::tuple_element_t<index, std::tuple<_Types...>>;
            return *std::any_cast<request<nth_type>>(&requests[index]);
        }

        template<uint32_t index>
        auto&
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
    };

    // change int errors to err::code
    template<typename... _Types>
    class promise
    {
        request_array<_Types...> _requests;
        std::optional<int> _error;
        const int _handle;

    public:
        promise(int error) :
            _error(error),
            _handle(0)
        {   }

        promise(int handle, const std::array<uint32_t, sizeof...(_Types)>& counts) :
            _requests(counts),
            _handle(handle)
        {   }

        template<uint32_t _Index>
        auto&
        get() const 
        {
            return _requests.template get<_Index>();
        }
        
        template<uint32_t _Index>
        auto&
        get() 
        {
            return _requests.template get<_Index>();
        }

        std::vector<std::string> 
        wait_for_completion() const
        {
            const auto req_count = _requests.request_count();
            std::vector<int> status(req_count); 
            auto* requests = const_cast<int*>(&_requests.ids[0]);

            const auto error = ncmpi_wait_all(_handle, req_count, requests, status.data());
            if (error == NC_NOERR)
            {
                std::vector<std::string> ret;
                ret.reserve(req_count);
                for (const auto& i : status) ret.push_back(std::string(ncmpi_strerror(i)));
                return ret;
            }
            std::cout << "error: " << ncmpi_strerror(error) << "\n";
            return { };
        }

        int* requests() { return &_requests.ids[0]; }

        uint32_t request_count() const { return sizeof...(_Types); }

        bool good() const
        {
            return (!_error.has_value());
        }
    };

    /*
    template<typename T>
    class promise
    {
        T* _value;
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
            _value = (T*)std::calloc(count, sizeof(T));
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

        value_type* value() { return _value; }
        const_value* value() const { return _value; }
    };
    */

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

namespace pio::type
{
    using Char = io::Type<NC_CHAR>;
    using Double = io::Type<NC_DOUBLE>;
    using Float = io::Type<NC_FLOAT>;
}