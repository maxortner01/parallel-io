#include <iostream>
#include <cmath>
#include <cassert>
#include <numeric>

#include "./pio/pio.hh"

using namespace pio;

#define mpi_assert(expr) if (!(expr)) { std::cout << "bad at " << __LINE__ << "\n"; MPI_Abort(MPI_COMM_WORLD, 2); return 2; }

template<typename T>
static void copy(
    const std::string& name,
    const std::vector<MPI_Offset>& start,
    const std::vector<MPI_Offset>& counts,
    const netcdf::file<io::access::ro>& in,
    netcdf::file<io::access::rw>& out)
{
    const auto promise = in.get_variable_values<T>(name, start, counts);
    assert(promise.good());
    promise.wait_for_completion();
    const auto count = promise.template get<0>().count;
    const auto* data = promise.template get<0>().value();
    const auto write_promise = out.write_variable<T>(name, data, count, start, counts);
    
    if (!write_promise.good()) { std::cout << name << " failed\n"; return;}

    write_promise.wait_for_completion();
}

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    
    io::distributor dist(MPI_COMM_WORLD);

    if (dist.rank() == 0)
    {
        // Read in the base exodus file
        exodus::file<std::size_t, io::access::ro> in("../box-hex.exo");
        assert(in);

        // Create the output exodus file and write over the init info 
        // from the input file
        exodus::file<std::size_t, io::access::wo> out("../test.exo");
        
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
    }

    // Halt all processes to ensure file creation is done
    MPI_Barrier(MPI_COMM_WORLD);

    {    
        // Read in the base exodus file
        netcdf::file<io::access::ro> in("../box-hex.exo");
        assert(in);

        // Create the output file
        netcdf::file<io::access::rw> f("../test.exo");
        assert(f.good());

        // Get the variable names
        const auto vars = in.variable_names();
        assert(vars.good());
        const auto var_names = vars.value();

        // Populate the distributor's volumes
        for (uint32_t i = 0; i < var_names.size(); i++)
        {
            // Grab variable info
            const auto var_info_r = in.get_variable_info(var_names[i]);
            assert(var_info_r);
            const auto& var_info = var_info_r.value();

            // Create the data volume corresponding to the variable
            io::distributor::volume volume;
            volume.data_index = i;
            volume.dimensions.reserve(var_info.dimensions.size());

            // Get the dimension information of this variable
            bool good = true;
            for (const auto& dim : var_info.dimensions)
            {   
                if (!dim.length) { good = false; break; }
                volume.dimensions.push_back(dim.length);
                volume.data_type = var_info.type;
            }

            // If none of the dimensions have zero length, add it to the list
            if (good) dist.data_volumes.push_back(volume);
        }

        {
            // with the distributor
            // we call 
            auto tasks_r = dist.get_tasks();
            assert(tasks_r.good());
            const auto& tasks = tasks_r.value();
            // then
            /*
              for (const auto& subvol : tasks)
              {
                const auto& name = var_names[subvol.volume_index];
                const auto var_info = in.get_variable_info(name);
                assert(var_info.good())
                switch (var_info.value().type)
                {
                case types::Int::nc:    copy<types::Int>(name, subvol.offsets, subvol.counts, in, f);    break;
                case types::Float::nc:  copy<types::Float>(name, subvol.offsets, subvol.counts, in, f);  break;
                case types::Double::nc: copy<types::Double>(name, subvol.offsets, subvol.counts, in, f); break;
                case types::Char::nc:   copy<types::Char>(name, subvol.offsets, subvol.counts, in, f);   break;
                }
              }      
            */

            // go through and copy
            const auto& name = var_names[dist.rank()];
            std::cout << name << "\n";
            const auto var_info = in.get_variable_info(name);
            assert(var_info.good());

            const auto dim_count = var_info.value().dimensions.size();
            
            std::vector<MPI_Offset> start(dim_count, 0);
            std::vector<MPI_Offset> counts(dim_count);
            for (uint32_t i = 0; i < dim_count; i++)
                counts[i] = var_info.value().dimensions[i].length;
            
            if (std::accumulate(counts.begin(), counts.end(), 0))
            {
                switch (var_info.value().type)
                {
                case types::Int::nc:    copy<types::Int>(name, start, counts, in, f);    break;
                case types::Float::nc:  copy<types::Float>(name, start, counts, in, f);  break;
                case types::Double::nc: copy<types::Double>(name, start, counts, in, f); break;
                case types::Char::nc:   copy<types::Char>(name, start, counts, in, f);   break;
                }
            }
        }
    }

    MPI_Finalize();
}