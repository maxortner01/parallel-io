#include <iostream>
#include <cmath>
#include <cassert>
#include "./pio/pio.hh"

using namespace pio;

#define mpi_assert(expr) if (!(expr)) { std::cout << "bad at " << __LINE__ << "\n"; MPI_Abort(MPI_COMM_WORLD, 2); return 2; }

/*
int main2(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    std::vector<float> data(10 * 10);
    uint32_t i = 0;
    for (auto& v : data) v = i++;

    const auto [rank, nprocs] = []()
    {
        int rank, nprocs;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
        return std::pair(rank, nprocs);
    }();

    {
        netcdf::file<io::access::wo> f("../test.cdf");
        mpi_assert(f.good());

        mpi_assert(f.define_dimension("x", 10));
        mpi_assert(f.define_dimension("y", 10));

        mpi_assert(f.define_variables<pio::type::Float>("v1", { "x", "y" }));
        f.end_define();

        //const auto adj = (rank == nprocs - 1?0:-1);
        //const auto partition_size = (int)std::round(10U / (double)nprocs) + adj;
        //const auto start = std::min(partition_size * rank, (int)data.size());
        
        std::vector<MPI_Offset> offsets(2);
        std::vector<MPI_Offset> counts(2);

        offsets[1] = 0;
        offsets[0] = rank * 5;
        counts[1] = 10;
        counts[0] = 5;

        const auto promise = f.write_variable<pio::type::Float>(
            "v1", 
            (data.data() + rank * (10 * 10 / 2)),
            10 * 10 / 2,
            offsets,
            counts
        );
        std::cout << promise.good() << " " << promise.error() << "\n";
        assert(promise.good());
        const auto status = promise.wait_for_completion();
        for (const auto& s : status) std::cout << s << "\n";
    }

    MPI_Finalize();

    return 0;
}*/

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    /*
    {
        std::unordered_map<std::string, std::size_t> dimensions;
        dimensions.insert(std::pair("len_string", 256));
        dimensions.insert(std::pair("time_step", NC_UNLIMITED));
        dimensions.insert(std::pair("num_dim", 3));

        netcdf::file<io::access::wo> out("../test.exo");
        for (const auto& p : dimensions)
            mpi_assert(out.define_dimension(p.first, p.second));
    }
    */

    // Read in the base exodus file
    exodus::file<std::size_t, io::access::ro> in("../box-hex.exo");
    assert(in);

    if (rank == 0)
    {
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

    MPI_Barrier(MPI_COMM_WORLD);

    {
        std::vector<std::string> var_names;
        {
            netcdf::file<io::access::ro> f("../test.exo");

            auto vars = f.variable_names();
            assert(vars.good());
            var_names = std::move(vars.value());
        }

        netcdf::file<io::access::rw> f("../test.exo");
        assert(f.good());

        /*
        for (uint32_t i = 1; i <= var_names.size(); i++)
        {
            auto ret = in.get_element_variable_values(1, i);
            std::cout << ret.error() << " " << ex_strerror(ret.error()) <<"\n";
            assert(ret.good()); 

            const auto& vals = ret.value();
            for (const auto& v : vals) 
            {
                for (const auto& w : v)
                    std::cout << w << " ";
                std::cout << "\n";
            }
        }*/
    }

    MPI_Finalize();
}