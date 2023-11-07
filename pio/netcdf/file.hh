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

    struct info
    {
        int dimensions, variables, attributes;
    };

    struct dimension
    {
        int id;
        MPI_Offset length;
        std::string name;
    };

    struct variable
    {
        int index, attributes, type;
        std::vector<dimension> dimensions;
    };

    struct value_info
    {
        uint32_t index, size;
        int type;
        value_info() : index(0), size(0) { }
    };

    template<typename _Type>
    struct GetData
    {
        std::size_t cell_count, request_count;
        std::shared_ptr<typename _Type::integral_type> data;
        std::shared_ptr<int> requests;
    };

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
        READ io::result<std::unordered_map<std::string, MPI_Offset>>
        get_dimension_lengths() const;

        READ io::result<info> 
        inquire() const;

        READ io::result<std::vector<std::string>>
        variable_names() const;

        READ io::result<variable>
        get_variable_info(const std::string& name) const;

        READ io::result<value_info>
        get_variable_value_info(const std::string& name) const;
        
        template<typename _Type, READ_TEMP>
        io::result<std::vector<typename _Type::integral_type>>
        read_variable_sync(
            const std::string& name,
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        template<typename _Type, READ_TEMP>
        const io::promise<io::access::ro, _Type>
        get_variable_values(
            const std::string& name, 
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        READ io::result<dimension>
        get_dimension(int id) const;

        READ io::result<dimension>
        get_dimension(const std::string& name) const;

        /* WRITE / READ-WRITE */
        template<typename _Type, WRITE_TEMP>
        const io::promise<io::access::wo, _Type>
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

    template<typename _Type, io::access _Read, io::access _Write>
    bool copy_variable(
        const std::string& name,
        const std::vector<MPI_Offset>& offsets,
        const std::vector<MPI_Offset>& counts,
        const file<_Read>& in,
        file<_Write>& out
        )
    {
        const auto data_r = in.template read_variable_sync<_Type>(name, offsets, counts);
        if (!data_r.good()) return false;
        const auto data = data_r.value();
        
        const auto res = out.template write_variable<_Type>(name, data.data(), data.size(), offsets, counts);
        if (!res.good()) return false;
        res.wait();

        return true;
    }

}