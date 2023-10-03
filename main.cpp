#include <iostream>
#include <cmath>

#include "./pio/pio.hh"

static int handle_error(int status, int lineno)
{
    std::cout << "Error at line " << lineno << ": " << ncmpi_strerror(status) << "\n";
    MPI_Abort(MPI_COMM_WORLD, 1);
    return status;
}

using namespace pio;
using namespace pio::netcdf;

#define mpi_assert(expr) if (!(expr)) { std::cout << "bad at " << __LINE__ << "\n"; MPI_Abort(MPI_COMM_WORLD, 2); return 2; }

int main(int argc, char** argv)
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
        file<io::access::wo> f("../test.cdf");
        mpi_assert(f.good());

        mpi_assert(f.define_dimension("x", 10));
        mpi_assert(f.define_dimension("y", 10));

        mpi_assert(f.define_variables<pio::type::Float>("v1", { "x", "y" }));
        f.end_define();

        const auto adj = (rank == nprocs - 1?0:-1);
        const auto partition_size = (int)std::round(10U / (double)nprocs) + adj;
        const auto start = std::min(partition_size * rank, (int)data.size());
        
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
}

int main2(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    const auto [rank, nprocs] = []()
    {
        int rank, nprocs;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
        return std::pair(rank, nprocs);
    }();

    MPI_Info info;
    MPI_Info_create(&info);
    MPI_Info_set(info, "nc_var_align_size", "1");

    int ncfile;
    auto err = ncmpi_create(MPI_COMM_WORLD, "../test.cdf", NC_CLOBBER | NC_64BIT_OFFSET, info, &ncfile);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    MPI_Info_free(&info);

    const auto ndims = 1;
    int dimid;
    int var1id, var2id;
    err = ncmpi_def_dim(ncfile, "d1", nprocs, &dimid);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    err = ncmpi_def_var(ncfile, "v1", NC_INT, ndims, &dimid, &var1id);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    err = ncmpi_def_var(ncfile, "v2", NC_INT, ndims, &dimid, &var1id);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    char buf[13] = "Hello World\n";
    err = ncmpi_put_att_text(ncfile, NC_GLOBAL, "string", 13, buf);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    err = ncmpi_enddef(ncfile);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    int requests[2];
    MPI_Offset start = rank;
    MPI_Offset count = 1;
    auto data1 = rank, data2 = rank;

    err = ncmpi_iput_vara(ncfile, var1id, &start, &count, &data1, count, MPI_INT, &requests[0]);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    err = ncmpi_iput_vara(ncfile, var2id, &start, &count, &data2, count, MPI_INT, &requests[1]);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    int statuses[2];
    err = ncmpi_wait_all(ncfile, 2, requests, statuses);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    for (uint32_t i = 0; i < 2; i++)
        std::cout << ncmpi_strerror(statuses[i]) << "\n";
    
    err = ncmpi_close(ncfile);
    if (err != NC_NOERR) return handle_error(err, __LINE__);

    MPI_Finalize();
    return 0;
}