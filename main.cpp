#include <iostream>
#include <cmath>
#include <cassert>
#include <functional>
#include <numeric>
#include <cstring>

#include "./pio/pio.hh"

using namespace pio;

#define mpi_assert(expr) if (!(expr)) { std::cout << "bad at " << __LINE__ << "\n"; MPI_Abort(MPI_COMM_WORLD, 2); return false; }

template<typename _Type, io::access file_access>
std::vector<int>
wait(
    const netcdf::file<file_access>& file,
    const netcdf::GetData<_Type>& data)
{
    std::vector<int> statuses(data.request_count, 0);
    assert(data.request_count && data.requests.get());
    std::cout << "Waiting on request " << data.requests.get()[0] << "\n";
    const auto err = ncmpi_wait(file.handle, data.request_count, data.requests.get(), statuses.data());
    if (err != NC_NOERR) { std::cout << ncmpi_strerror(err) << "\n"; assert(false); }
    return statuses;
}

template<typename _Type, io::access file_access>
std::vector<int>
wait(
    const netcdf::file<file_access>& file,
    int* requests,
    const std::size_t& request_count)
{
    std::vector<int> statuses(request_count, 0);
    std::cout << "Waiting on write at request " << *requests << "\n";
    const auto err = ncmpi_wait(file.handle, request_count, requests, statuses.data());
    if (err != NC_NOERR) { std::cout << ncmpi_strerror(err) << "\n"; assert(false); }
    std::cout << "Done\n";
    return statuses;
}

template<typename _Type>
std::vector<typename _Type::integral_type>
read(
    const std::string& name,
    const std::vector<MPI_Offset>& offsets,
    const std::vector<MPI_Offset>& counts,
    const netcdf::file<io::access::ro>& in)
{
    auto err = ncmpi_begin_indep_data(in.handle);
    if (err != NC_NOERR) { std::cout << "Err: " << ncmpi_strerror(err) << "\n"; assert(false); }

    const auto read_in = in.get_variable_values<_Type>(name, offsets, counts);
    assert(read_in.good());
    const auto& data = read_in.value();
    auto statuses = wait(in, data);
    
    err = ncmpi_end_indep_data(in.handle);
    if (err != NC_NOERR) { std::cout << "Err: " << ncmpi_strerror(err) << "\n"; assert(false); }
    
    using data_type = typename _Type::integral_type;
    std::vector<data_type> ret(data.cell_count);
    std::memcpy(ret.data(), data.data.get(), sizeof(data_type) * data.cell_count);
    return ret;
}

template<typename _Type>
bool write(
    const std::string& name,
    const std::vector<MPI_Offset>& offsets,
    const std::vector<MPI_Offset>& counts,
    netcdf::file<io::access::rw>& out,
    const typename _Type::integral_type* data,
    const std::size_t& cell_count)
{
    auto err = ncmpi_begin_indep_data(out.handle);
    if (err != NC_NOERR) { std::cout << "Err: " << ncmpi_strerror(err) << "\n"; return false; }

    const auto res = out.write_variable<_Type>(name, data, cell_count, offsets, counts);
    if (!res.good()) { std::cout << "Failed with code " << res.error() << "\n"; return false; }
    const auto& requests = res.value();
    const auto statuses = wait<_Type>(out, requests.get(), 1);
    err = ncmpi_end_indep_data(out.handle);
    if (err != NC_NOERR) { std::cout << "Err: " << ncmpi_strerror(err) << "\n"; return false; }

    return true;
}

template<typename _Type>
void copy(
    const std::string& name,
    const std::vector<MPI_Offset>& offsets,
    const std::vector<MPI_Offset>& counts,
    const netcdf::file<io::access::ro>& in,
    netcdf::file<io::access::rw>& out)
{
    const auto data = read<_Type>(name, offsets, counts, in);
    assert(write<_Type>(name, offsets, counts, out, data.data(), data.size()));
}

bool 
duplicate_file(
    const std::string& in_name, 
    const std::string& out_name)
{
    // Read in the base exodus file
    exodus::file<std::size_t, io::access::ro> in(in_name);
    assert(in);

    // Create the output exodus file and write over the init info 
    // from the input file
    exodus::file<std::size_t, io::access::wo> out(out_name);
    
    if (out.good())
    {
        const auto info = in.get_info();
        assert(info.good());

        // Copy over paramters
        mpi_assert(out.set_init_params(info.value()));

        // Write a time step
        mpi_assert(out.write_time_step(0));

        // Get the element blocks and copy them over
        const auto blocks = in.get_blocks();
        assert(blocks.good());

        for (const auto& block : blocks.value())
            assert(out.create_block(block));
    }

    return true;
}

/*

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    io::distributor dist(MPI_COMM_WORLD);

    if (!dist.rank()) assert(duplicate_file("../box-hex.exo", "../test.exo"));
    MPI_Barrier(MPI_COMM_WORLD);

    {
        // Load the input file
        netcdf::file<io::access::ro> in("../box-hex.exo");
        assert(in);
        
        // Create the output file
        netcdf::file<io::access::rw> f("../test.exo");
        assert(f);

        // Generate a list of variable names that are in both files after
        // duplication
        const auto shared_names = [&]()
        {
            const auto in_vars_res  = in.variable_names();
            const auto out_vars_res = f.variable_names();
            assert(in_vars_res.good());
            assert(out_vars_res.good());
            const auto& in_vars = in_vars_res.value();
            const auto& out_vars = out_vars_res.value();

            std::vector<std::string> ret;
            std::copy_if(in_vars.begin(), in_vars.end(), std::back_inserter(ret), 
            [&](const auto& val)
            {
                const auto it = std::find(out_vars.begin(), out_vars.end(), val);
                const auto in = std::find(ret.begin(), ret.end(), val);
                return (it != out_vars.end()) && (in == ret.end());
            });
            
            std::copy_if(out_vars.begin(), out_vars.end(), std::back_inserter(ret), 
            [&](const auto& val)
            {
                const auto it = std::find(in_vars.begin(), in_vars.end(), val);
                const auto in = std::find(ret.begin(), ret.end(), val);
                return (it != in_vars.end()) && (in == ret.end());
            });

            return ret;
        }();

        // Generate the volumes in the distributor
        uint32_t i = 0;
        for (const auto& name : shared_names)
        {
            // Get the variable type and dimension info for the volume
            const auto var_info_in = in.get_variable_info(name);
            assert(var_info_in.good());
            const auto& var_info = var_info_in.value();

            io::distributor::volume vol;
            vol.data_index = i++;
            vol.data_type  = var_info.type;
            for (uint32_t i = 0; i < var_info.dimensions.size(); i++)
                vol.dimensions.push_back(var_info.dimensions[i].length);
            dist.data_volumes.push_back(vol);
        }

        auto subvols_in = dist.get_tasks();
        assert(subvols_in.good());
        const auto& subvols = subvols_in.value();
        std::cout << "Generated " << subvols.size() << " subvolume(s).\n";
        for (uint32_t s = 0; s < subvols.size(); s++)
        {
            std::cout << "Subvolume " << s + 1 << " (" << shared_names[subvols[s].volume_index] << "):\n";
            for (uint32_t d = 0; d < subvols[s].counts.size(); d++)
            {
                std::cout << "  Dimension " << d + 1 << ": From cell " << subvols[s].offsets[d] << " to cell " << subvols[s].offsets[d] + subvols[s].counts[d] << ".\n";
            }
        }

        for (auto& subvol : subvols)
        {
            const auto& data_vol = dist.data_volumes[subvol.volume_index];
            const auto _name = shared_names[data_vol.data_index];

            if (subvol.volume_index) continue;

            switch (data_vol.data_type)
            {
            case types::Int::nc:    copy<types::Int>(_name, subvol.offsets, subvol.counts, in, f);    break;
            case types::Float::nc:  copy<types::Float>(_name, subvol.offsets, subvol.counts, in, f);  break;
            case types::Double::nc: copy<types::Double>(_name, subvol.offsets, subvol.counts, in, f); break;
            case types::Char::nc:   copy<types::Char>(_name, subvol.offsets, subvol.counts, in, f);   break;
            }
        }   
    }
    
    MPI_Finalize();
}*/

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    io::distributor dist(MPI_COMM_WORLD);

    if (!dist.rank()) assert(duplicate_file("../box-hex.exo", "../test.exo"));
    MPI_Barrier(MPI_COMM_WORLD);

    {
        netcdf::file<io::access::ro> in("../box-hex.exo");
        assert(in);
        
        // Create the output file
        netcdf::file<io::access::rw> f("../test.exo");
        assert(f);

        // Grab the names of the variables
        const auto vars = in.variable_names();
        assert(vars.good());
        const auto& var_names = vars.value();

        netcdf::file<io::access::rw> out("../test.exo");
        assert(out);

        // Get the info about this variable's dimensions
        const auto add_vol = [&](int index) {
            const auto& name = var_names[index];
            const auto var_info_in = in.get_variable_info(name);
            assert(var_info_in.good());
            const auto& var_info = var_info_in.value();

            io::distributor::volume vol;
            vol.data_index = index;
            vol.data_type  = var_info.type;

            // Construct the offset and count vectors
            std::vector<MPI_Offset> offsets(var_info.dimensions.size(), 0);
            std::vector<MPI_Offset> counts(var_info.dimensions.size(), 0);
            for (uint32_t i = 0; i < counts.size(); i++)
            {
                counts[i] = var_info.dimensions[i].length;
                vol.dimensions.push_back(counts[i]);
            }
            dist.data_volumes.push_back(vol);
        };

        std::cout << var_names[7] << "\n";
        add_vol(7);
        std::cout << var_names[8] << "\n";
        add_vol(8);

        auto subvols_in = dist.get_tasks();
        assert(subvols_in.good());
        const auto& subvols = subvols_in.value();
        std::cout << "Generated " << subvols.size() << " subvolume(s).\n";
        for (uint32_t s = 0; s < subvols.size(); s++)
        {
            std::cout << "Subvolume " << s + 1 << ":\n";
            for (uint32_t d = 0; d < subvols[s].counts.size(); d++)
            {
                std::cout << "  Dimension " << d + 1 << ": From cell " << subvols[s].offsets[d] << " to cell " << subvols[s].offsets[d] + subvols[s].counts[d] << ".\n";
            }
        }

        for (auto& subvol : subvols)
        {
            const auto& data_volume = dist.data_volumes[subvol.volume_index];
            const auto& name = var_names[data_volume.data_index];
            switch (data_volume.data_type)
            {
            case types::Int::nc:    copy<types::Int>(name, subvol.offsets, subvol.counts, in, f);    break;
            case types::Float::nc:  copy<types::Float>(name, subvol.offsets, subvol.counts, in, f);  break;
            case types::Double::nc: copy<types::Double>(name, subvol.offsets, subvol.counts, in, f); break;
            case types::Char::nc:   copy<types::Char>(name, subvol.offsets, subvol.counts, in, f);   break;
            }
        }   
    }
    
    MPI_Finalize();
}
