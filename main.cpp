#include <iostream>
#include <mpi.h>
#include <exodusII.h>
#include <pnetcdf.h>
#include <optional>

namespace pncdf
{
    template<typename T>
    class result
    {
        int err;
        std::optional<T> _value;
    
    public:
        result(int e) : _value(std::nullopt), err(e)
        {   }

        result(const T& val) : _value(val), err(NC_NOERR)
        {   }

        auto good() const { return _value.has_value(); }
        auto error_string() const { return std::string(ncmpi_strerror(err)); }

        auto& value() const { return _value.value(); }
    };

    struct file
    {
        struct info
        {
            int dimensions, variables, attributes;
        };

        file(const std::string& file_location)
        {
            err = ncmpi_open(
                MPI_COMM_WORLD, 
                file_location.c_str(),
                NC_NOWRITE,
                MPI_INFO_NULL,
                &handle
            );

            if (err != NC_NOERR) _good = false;
            else _good = true;
        }

        ~file()
        { close(); }

        void close()
        {
            if (good())
                err = ncmpi_close(handle);
        }

        result<info> inquire() const
        {
            info i;
            int unlimited;
            int error = ncmpi_inq(handle, &i.dimensions, &i.variables, &i.attributes, &unlimited);

            if (error != NC_NOERR) return result<info>(error);
            
            return result<info>(i);
        }

        auto error_string() const { return std::string(ncmpi_strerror(err)); }
        auto error() const { return err; }
        bool good()  const { return _good; }

        operator bool() const
        {
            return good();
        }

    private:
        int handle, err;
        bool _good;
    };
}

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    printf("Hello from processor %s, rank %d out of %d processors.\n", processor_name, world_rank, world_size);

    int comp_ws = sizeof(float);
    int io_ws = 0;
    float version;
    const auto exoid = ex_open("../box-hex.exo", EX_READ, &comp_ws, &io_ws, &version);
    if (exoid < 0)
    {
        std::cout << "Error opening file \n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 2;
    }

    char title[MAX_LINE_LENGTH];
    int num_dim, num_nodes, num_elem, num_elem_blk, num_node_sets, num_side_sets;
    const auto init = ex_get_init(exoid, title, &num_dim, &num_nodes, &num_elem, &num_elem_blk, &num_node_sets, &num_side_sets);
    if (init < 0)
    {
        std::cout << "Error occurred " << ex_strerror(init) << "\n";
    }
    else
    {
        std::cout << "Title: " << title << "\n";
        std::cout << "Dimensions: " << num_dim << "\n";
        std::cout << "Nodes: " << num_nodes << "\n";
        std::cout << "Num Elems: " << num_elem << "\n";
        std::cout << "Num Elem Blk: " << num_elem_blk << "\n";
        std::cout << "Num Node Sets: " << num_node_sets << "\n";
        std::cout << "Num Side Sets: " << num_side_sets << "\n";
    }

    const auto ret = ex_close(exoid);
    if (ret)
    {
        std::cout << "Closed with warning or error\n";
    }

    /*
    pncdf::file exo("../box-hex.exo");
    if (!exo)
    {
        std::cout << "Error opening file: " << exo.error_string() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    const auto inq = exo.inquire();

    std::cout << "File info:\nndims: " << inq.value().dimensions << "\nnvars: " << inq.value().variables << "\nngatt: " << inq.value().attributes << "\n";

    exo.close();*/

    MPI_Finalize();
}