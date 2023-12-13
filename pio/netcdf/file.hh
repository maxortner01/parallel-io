/**
 * @file file.hh
 * @author Max Ortner (mortner@lanl.gov)
 * @brief Reading and writing to a NetCDF file.
 * 
 * This represents a NetCDF file, but uses PNetCDF to read and write to it, taking
 * full advantage of the parallel I/O system
 * 
 * @version 0.1
 * @date 2023-10-05
 * 
 * @copyright Copyright (c) 2023, Triad National Security, LLC
 * 
 */

#pragma once

#include "../io.hh"

#include <unordered_map>
#include <vector>
#include <string>

#define READ_TEMP typename = std::enable_if<_Access == io::access::ro || _Access == io::access::rw, bool>
#define READ template<READ_TEMP>

#define WRITE_TEMP typename = std::enable_if<_Access == io::access::wo || _Access == io::access::rw, bool>
#define WRITE template<WRITE_TEMP>

namespace pio::netcdf
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

    /// The basic information describing this exodus file.
    struct info
    {
        int dimensions, variables, attributes;
    };

    /// A singular degree of freedom 
    struct dimension
    {
        int id;
        MPI_Offset length;
        std::string name;
    };

    /// A value that has dimensions and is data stored in the file
    struct variable
    {
        int index, attributes, type;
        std::vector<dimension> dimensions;
    };

    /// Information about the type of data stored in a variable entry
    struct value_info
    {
        uint32_t index, size;
        int type;
        value_info() : index(0), size(0) { }
    };

    // unused
    template<typename _Type>
    struct GetData
    {
        std::size_t cell_count, request_count;
        std::shared_ptr<typename _Type::integral_type> data;
        std::shared_ptr<int> requests;
    };

    /// Basic storage for errors, contains both PIO errors and pnetcdf errors
    struct error_code
    {
        enum code
        {
            TypeMismatch,
            SizeMismatch,
            DimensionSizeMismatch,
            NullData
        };

        /**
         * @brief Initialize with a PIO error
         * @param c PIO error
         */
        error_code(code c);

        /**
         * @brief Initialize with pnetcdf error
         * @param c 
         */
        error_code(int c);

        /// Convert the error to string
        std::string message() const;

    private:
        union
        {
            code _code;
            int  _netcdf;
        };
        bool _netcdf_error;

        static std::string _to_string(code c);
    };
    
    static error_code netcdf_error(int num)
    {
        return error_code(num);
    }
    
    template<typename T>
    using result = io::result<T, error_code>;
    
    template<io::access _Access, typename... _Types>
    using promise = io::promise<_Access, error_code, _Types...>;

    template<io::access _Access>
    struct file
    {

        file(const std::string& filename);
        ~file();

        auto error_string() const { return std::string(ncmpi_strerror(err)); }

        void close();

        bool error()    const { return err; }
        auto good()     const { return _good; }
        operator bool() const { return good(); }
        
        /* READ / READ-WRITE */

        /**
         * @brief Get the lengths of each dimension in the file
         * 
         * This method is for read only or read-write files.
         * 
         * @return result<std::unordered_map<std::string, MPI_Offset>> Error or a map of dimension names to their lengths
         */
        READ result<std::unordered_map<std::string, MPI_Offset>>
        get_dimension_lengths() const;

        /**
         * @brief Get basic info about the file
         * 
         * This method is for read only or read-write files.
         * 
         * @return result<info> Error or a collection of basic info about the file
         */
        READ result<info> 
        inquire() const;

        /**
         * @brief Get the names of all of the variables
         * 
         * This method is for read only or read-write files.
         * 
         * @return result<std::vector<std::string>> Error or a list of variable names
         */
        READ result<std::vector<std::string>>
        variable_names() const;

        /**
         * @brief Get the features of a variable
         * 
         * This method is for read only or read-write files.
         * 
         * @param name Name of the variable
         * @return result<variable> Error or the information about a variable 
         */
        READ result<variable>
        get_variable_info(const std::string& name) const;

        /**
         * @brief Get the information about the data a variable describes
         * 
         * This method is for read only or read-write files
         * 
         * @param name Name of the variable
         * @return result<value_info> Error or the information about the variable values
         */
        READ result<value_info>
        get_variable_value_info(const std::string& name) const;
        
        // TODO: Possibly make the return type a 2d ragged array which is the 
        // data for each dimension
        /**
         * @brief Blocking method that reads a requested data region
         * 
         * This method is for read only or read-write files
         * 
         * If the requested data type is different than the data contained in the file
         * the result will contain a @ref error_code::TypeMismatch error.
         * 
         * @tparam _Type The data-type of the variable from @ref pio::types
         * @param name  The name of the variable
         * @param start The starting indices for each dimension (must be same length as dimensions of this variable)
         * @param count The amount of objects after the starting index for each dimension (must be same length as start)
         * @return result<std::vector<typename _Type::integral_type>> Error or the requested data region
         */
        template<typename _Type, READ_TEMP>
        result<std::vector<typename _Type::integral_type>>
        read_variable_sync(
            const std::string& name,
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        /**
         * @brief Asynchronous request to read a requested data region
         * 
         * This method is for read only or read-write files
         * 
         * If the requested data type is different than the data contained in the file
         * the result will contain a @ref error_code::TypeMismatch error.
         * 
         * @tparam _Type The data-type of the variable from @ref pio::types
         * @param name  The name of the variable
         * @param start The starting indices for each dimension (must be same length as dimensions of this variable)
         * @param count The amount of objects after the starting index for each dimension (must be same length as start)
         * @return const promise<io::access::ro, _Type> Error or a promise containing the requested data
         */
        template<typename _Type, READ_TEMP>
        const promise<io::access::ro, _Type>
        get_variable_values(
            const std::string& name, 
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        /**
         * @brief Get the dimension information of an id
         * 
         * This method is for read only or read-write files
         * 
         * @param id The id of the dimension
         * @return result<dimension> Error or the dimension information
         */
        READ result<dimension>
        get_dimension(int id) const;

        
        /**
         * @brief Get the dimension information from a name
         * 
         * This method is for read only or read-write files
         * 
         * @param name The name of the dimension
         * @return result<dimension> Error or the dimension information
         */
        READ result<dimension>
        get_dimension(const std::string& name) const;

        /* WRITE / READ-WRITE */

        /**
         * @brief Asynchronous request to write given data to a region in the file
         * 
         * This method is for write only or read-write files
         * 
         * @tparam _Type The data type of the variable from @ref pio::types
         * @param name   The name of the variable
         * @param data   Pointer to the data to write into the file
         * @param size   The length of the data
         * @param offset The starting indices for each dimension (must be same length as dimensions of this variable)
         * @param count  The amount of objects after the starting index for each dimension (must be same length as start)
         * @return const promise<io::access::wo, _Type> Error or a promise to complete the write request
         */
        template<typename _Type, WRITE_TEMP>
        const promise<io::access::wo, _Type>
        write_variable(
            const std::string& name,
            const typename _Type::integral_type* data,
            const std::size_t& size,
            const std::vector<MPI_Offset>& offset,
            const std::vector<MPI_Offset>& count);

        int get_handle() const { return handle; }
    private:
        int handle, err;
        bool _good;
    };

    // TODO: Make this return a result so we can communicate more informative errors
    /**
     * @brief Copy the requested data region from one file to another
     * 
     * The variable should exist in both the input and the output file and their dimensions should
     * be the same.
     * 
     * @tparam _Type  The type of the data from @ref io::types
     * @tparam _Read  Access of in file (should be @ref io::access::ro or @ref io::access::rw)
     * @tparam _Write Access of out file (should be @ref io::access::wo or @ref io::access::rw)
     * @param name    The name of the variable to copy
     * @param offsets The starting indices for each dimension (must be same length as dimensions of this variable)
     * @param counts  The amount of objects after the starting index for each dimension (must be same length as start)
     * @param in  The input file
     * @param out The output file
     * @return true  The copy was successful
     * @return false The copy was not successful
     */
    template<typename _Type, io::access _Read, io::access _Write>
    static bool copy_variable(
        const std::string& name,
        const std::vector<MPI_Offset>& offsets,
        const std::vector<MPI_Offset>& counts,
        const file<_Read>& in,
        file<_Write>& out
        )
    {
        const auto data_r = in.template read_variable_sync<_Type>(name, offsets, counts);
        if (!data_r) { std::cout << data_r.error().message() << "\n"; return false; }
        const auto& data = data_r.value();
        
        const auto res = out.template write_variable<_Type>(name, data.data(), data.size(), offsets, counts);
        if (!res) { std::cout << res.error().message() << "\n"; return false; }
        res.wait();

        return true;
    }

}