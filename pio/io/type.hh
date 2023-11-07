#pragma once

#include "../external.hh"

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
        ro = 0b01,
        wo = 0b10,
        rw = 0b11
    };

    // Does acc have write access?
    constexpr bool write_access(access acc)
    {
        return (int)acc & (int)access::wo;
    }

    constexpr access reduce_access(access acc)
    {
        return (write_access(acc) ? io::access::wo : io::access::ro);
    }

    /**
     * @brief An isomorphism of primitive data-types to MPI/NC data types.
     * 
     * @tparam The NC data type
     */
    template<nc_type T>
    struct type
    {   };
    
    template<typename T>
    using func_ptr = int(*)(int, int, const MPI_Offset*, const MPI_Offset*, T*, int*);

    /** @copydoc type */
    template<>
    struct type<NC_DOUBLE> 
    { 
        const static nc_type nc = NC_DOUBLE;
        using integral_type = double; 
        const static func_ptr<integral_type> func;
    };

    /** @copydoc Type */
    template<>
    struct type<NC_CHAR> 
    { 
        const static nc_type nc = NC_CHAR;
        using integral_type = char; 
        const static func_ptr<integral_type> func;
    };
    
    /** @copydoc Type */
    template<>
    struct type<NC_FLOAT> 
    { 
        const static nc_type nc = NC_FLOAT;
        using integral_type = float; 
        const static func_ptr<integral_type> func;
    };
    
    /** @copydoc Type */
    template<>
    struct type<NC_INT> 
    { 
        const static nc_type nc = NC_INT;
        using integral_type = int; 
        const static func_ptr<integral_type> func;
    };

#define CASE_SIZE_TYPE(t) case t: return sizeof(io::type<t>::integral_type)

    static constexpr std::size_t nc_sizeof(nc_type type)
    {
        switch (type)
        {
        CASE_SIZE_TYPE(NC_CHAR);
        CASE_SIZE_TYPE(NC_DOUBLE);
        CASE_SIZE_TYPE(NC_FLOAT);
        CASE_SIZE_TYPE(NC_INT);
        }
        return 0;
    }

#undef CASE_SIZE_TYPE
}

namespace pio::types
{
    using Double = io::type<NC_DOUBLE>;
    using Float = io::type<NC_FLOAT>;
    using Char = io::type<NC_CHAR>;
    using Int = io::type<NC_INT>;
}