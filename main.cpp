#include <iostream>
#include <cmath>
#include <cassert>
#include <functional>
#include <numeric>
#include <cstring>
#include <set>

#include "./pio/pio.hh"

using namespace pio;

#define mpi_assert(expr) if (!(expr)) { std::cout << "bad at " << __LINE__ << "\n"; MPI_Abort(MPI_COMM_WORLD, 2); return false; }

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
        const auto& in_var_names = vars.value();

        netcdf::file<io::access::rw> out("../test.exo");
        assert(out);

        const auto vars_out = out.variable_names();
        assert(vars_out.good());
        const auto& out_var_names = vars_out.value();

        std::vector<std::string> names;
        [&]()
        {
            for (const auto& val : in_var_names)
            {
                const auto it_out = std::find(out_var_names.begin(), out_var_names.end(), val);
                const auto it = std::find(names.begin(), names.end(), val);
                if (it_out != out_var_names.end() && it == names.end()) names.push_back(val);
            }
            
            for (const auto& val : out_var_names)
            {
                const auto it_out = std::find(in_var_names.begin(), in_var_names.end(), val);
                const auto it = std::find(names.begin(), names.end(), val);
                if (it_out != in_var_names.end() && it == names.end()) names.push_back(val);
            }
        }();
        for (const auto& name : names) std::cout << name << "\n";


        // Get the info about this variable's dimensions
        const auto add_vol = [&](int index) {
            const auto& name = names[index];
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
                if (!counts[i]) return;
                vol.dimensions.push_back(counts[i]);
            }
            dist.data_volumes.push_back(vol);
        };

        for (uint32_t i = 0; i < names.size(); i++)
            add_vol(i);

        auto subvols_in = dist.get_tasks();
        assert(subvols_in.good());
        const auto& subvols = subvols_in.value();

        for (auto& subvol : subvols)
        {
            const auto& data_volume = dist.data_volumes[subvol.volume_index];
            const auto& name = names[data_volume.data_index];
            
            switch (data_volume.data_type)
            {
            case types::Int::nc:    netcdf::copy_variable<types::Int>(name, subvol.offsets, subvol.counts, in, f);    break;
            case types::Float::nc:  netcdf::copy_variable<types::Float>(name, subvol.offsets, subvol.counts, in, f);  break;
            case types::Double::nc: netcdf::copy_variable<types::Double>(name, subvol.offsets, subvol.counts, in, f); break;
            case types::Char::nc:   netcdf::copy_variable<types::Char>(name, subvol.offsets, subvol.counts, in, f);   break;
            }
        }   
    }
    
    MPI_Finalize();
}
