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

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0)
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

        //const auto vars = in.variable_names();
        //assert(vars.good());
        //const auto var_names = vars.value();

        // Set a constant list of variables for the demonstration
        // run with mpirun -n 3 ./pio which will give each mpi process
        // its own variable
        std::vector<std::string> var_names = {"connect1", "connect2", "coor_names"};

        {
            // go through and copy
            const auto& name = var_names[rank];
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
                case type::Int::nc:    copy<type::Int>(name, start, counts, in, f);    break;
                case type::Float::nc:  copy<type::Float>(name, start, counts, in, f);  break;
                case type::Double::nc: copy<type::Double>(name, start, counts, in, f); break;
                case type::Char::nc:   copy<type::Char>(name, start, counts, in, f);   break;
                }
            }
        }
    }

    MPI_Finalize();
}