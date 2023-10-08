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
        const io::promise<_Type>
        get_variable_values(
            const std::string& name, 
            const std::vector<MPI_Offset>& start,
            const std::vector<MPI_Offset>& count) const;

        READ io::result<dimension>
        get_dimension(int id) const;

        READ io::result<dimension>
        get_dimension(const std::string& name) const;

        // Each variable is different type and has unique memory region
        /*
        template<typename... _Types, READ_TEMP>
        const io::promise<_Types...>
        get_variable_values(const std::array<std::string, sizeof...(_Types)>& names)
        {
            constexpr auto TYPE_COUNT = sizeof...(_Types);
            static_assert(TYPE_COUNT);

            // Set up the array of counts
            std::array<uint32_t, TYPE_COUNT> sizes;

            std::vector<io::result<value_info>> infos;
            infos.reserve(TYPE_COUNT);
            for (uint32_t i = 0; i < TYPE_COUNT; i++)
            {
                auto info = get_variable_value_info(names[i]);
                assert(info.good());

                sizes[i] = info.value().size;
                infos.push_back(info);
            }

            // Allocate the data to be retreived
            io::promise<_Types...> promise(handle, sizes);

            // "Iterate" through and make a request for each type supplied
            impl::static_for<TYPE_COUNT>([&](auto n) {
                constexpr std::size_t I = n;

                using _Type = impl::NthType<I, _Types...>;
                assert(infos[I].value().type == _Type::nc);

                // once we fill out start and count this should be it... 
                // we need to check if .good is true, if it's not 
                // maybe promise.template get<I>().make_error(info.error()); where make_error is a non-const request method

                const auto err = _Type::func(
                    handle, 
                    infos[I].value().index,
                    infos[I].value().start.data(),
                    infos[I].value().count.data(),
                    promise.template get<I>().value(),
                    &promise.requests()[I]
                );
            });

            return promise;
        }*/

        /* WRITE / READ-WRITE */
        template<typename _Type, WRITE_TEMP>
        const io::promise<_Type>
        write_variable(
            const std::string& name,
            const typename _Type::integral_type* data,
            const std::size_t& size,
            const std::vector<MPI_Offset>& offset,
            const std::vector<MPI_Offset>& count);


    private:
        int handle, err;
        bool _good;
    };
}