#pragma once

#include <pnetcdf.h>
#include <sstream>
#include <vector>
#include <optional>

namespace pio::io
{
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