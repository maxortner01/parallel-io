#include <iostream>
#include "./pio/pio.hh"

using namespace pio;

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

    exodus::file<float, io::access::ro> file("../box-hex.exo");
    if (!file)
    {
        std::cout << "Error opening file\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 2;
    }

    const auto res = file.get_info();
    
    if (res.good())
    {
        std::cout << "Title: " << res.value().title << "\n";
        std::cout << "Dimensions: " << res.value().num_dim << "\n";
        std::cout << "Nodes: " << res.value().num_nodes << "\n";
        std::cout << "Num Elems: " << res.value().num_elem << "\n";
        std::cout << "Num Elem Blk: " << res.value().num_elem_blk << "\n";
        std::cout << "Num Node Sets: " << res.value().num_node_sets << "\n";
        std::cout << "Num Side Sets: " << res.value().num_side_sets << "\n";
    }

    const auto coordinates = file.get_node_coordinates();

    if (coordinates.good())
    {
        const auto& val = coordinates.value();

        for (uint32_t i = 0; i < val.x.size(); i++)
        {
            std::cout << val.x[i] << " ";
            if (val.y.size()) std::cout << val.y[i] << " ";
            if (val.z.size()) std::cout << val.z[i] << " ";
            std::cout << "\n";
        }
    }

    const auto time = file.get_time_values();
    if (time.good())
    {
        std::cout << "Time value count: " << time.value().size() << "\n";
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