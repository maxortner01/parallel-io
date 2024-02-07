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
#include <functional>

/// All of the functionality for handling NetCDF files
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
            DimensionDoesntExist,
            NullData,
            NullFile,
            VariableDoesntExist,
            FailedTaskCreation
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

    /// \brief A NetCDF file
    /// \todo Add a file_type enum that specifies whether the currently contained exodus_file struct exists or not
    template<io::access _Access>
    struct file
    {
        struct exodus_file
        {
            /// \brief Copy data into memory for a given ExodusII variable
            /// \todo The returned errors are not specific enough to be super helpful
            READ result<std::vector<std::string>>
            get_variables() const;

            /// \brief Copy the node coordinates into memory
            /// \param get_data Whether or not to read the actual node coordinates, or just read the names
            /// \note This is a blocking method
            /// \todo Should we determine if its 64/32bit?
            READ result<std::unordered_map<std::string, std::vector<double>>>
            get_node_coordinates(bool get_data = true) const;

            /// \brief Write the corodinate node data into the exodus file
            /// \param comm The MPI comm channel to use for PIO
            /// \return List of shared pointers containing the \ref promise for each data region
            WRITE result<std::vector<std::shared_ptr<const promise<io::access::wo, types::Double>>>>
            write_node_coordinates(MPI_Comm comm, const std::unordered_map<std::string, std::vector<double>>& data);

        private:
            friend class file;

            exodus_file(file* base_file);

            file* _file;
        } exodus;

        file(const std::string& filename);
        
        file(const file&) = delete;
        file(file&&) = delete;
        
        ~file();

        auto error_string() const { return std::string(ncmpi_strerror(err)); }

        void close();

        bool error()    const { return err; }
        auto good()     const { return _good; }
        operator bool() const { return good(); }
        
        /* READ / READ-WRITE */

        /// Get the lengths of each dimension in the file
        READ result<std::unordered_map<std::string, MPI_Offset>>
        get_dimension_lengths() const;

        /// Get basic info about the file
        READ result<info> 
        inquire() const;

        /// Get the names of all of the variables
        READ result<std::vector<std::string>>
        variable_names() const;

        /// Get the features of a variable
        READ result<variable>
        get_variable_info(const std::string& name) const;

        /// Get the information about the data a variable describes
        READ result<value_info>
        get_variable_value_info(const std::string& name) const;
        
        // TODO: Possibly make the return type a 2d ragged array which is the 
        // data for each dimension
        /// Copy a section of the file into memory \note This is a blocking method
        template<typename _Type, READ_TEMP>
        result<std::vector<typename _Type::integral_type>>
        read_variable_sync(
            const std::string& name,
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        /// Produces an asynchronous request to copy a section of data from the file into memory
        template<typename _Type, READ_TEMP>
        const promise<io::access::ro, _Type>
        get_variable_values(
            const std::string& name, 
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        /// Get a dimension by id
        READ result<dimension>
        get_dimension(int id) const;

        
        /// Get a dimension by name
        READ result<dimension>
        get_dimension(const std::string& name) const;

        /* WRITE / READ-WRITE */

        /// Defines a new variable in the file \note The file must be in define mode or else this will give an error
        template<typename _Type, WRITE_TEMP>
        result<void>
        define_variable(const std::string& name, const std::vector<std::string>& dim_names);

        /// Execute a routine within define mode \note The file is put into and out of define mode in the scope of this method
        WRITE result<void>
        define(std::function<result<void>()> function);

        /// Produces an asynchronous request to write a section of data to a variable
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

    static std::vector<std::string> format(
        const std::vector<char>& data,
        const std::size_t& count,
        const std::size_t& str_len)
    {
        std::vector<std::string> r;
        r.reserve(str_len);

        for (uint32_t i = 0; i < count; i++)
        {
            std::string s;

            for (uint32_t j = 0; j < str_len; j++)
            {
                const auto& c = data[i * str_len + j];
                if (c) s += data[i * str_len + j];
            }

            r.push_back(std::move(s));
        }

        return r;
    }

}