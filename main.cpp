#include <iostream>
#include "./pio/pio.hh"

using namespace pio;

int exodus_file()
{
    exodus::file<float, io::access::ro> file("../box-hex-colors.exo");
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

    // https://www.osti.gov/servlets/purl/10102115
    const auto time = file.get_time_values();
    if (time.good())
    {
        std::cout << "Time value count: " << time.value().size() << "\n";
        for (const auto& val : time.value()) std::cout << val << "\n";
    }

    const auto variable_names = file.get_variable_names(exodus::scope::element).value();
    std::cout << variable_names[0] << "\n";

    const auto val = file.get_element_variable_values(1, 1);
    if (val.good())
    {
        for (const auto& v : val.value())
        {
            for (const auto& c : v) std::cout << c << " ";
            std::cout << "\n";
        }
    }
}

int pncdf_file()
{
    netcdf::file exo("../box-hex-colors.exo", io::access::ro);
    if (!exo)
    {
        std::cout << "Error opening file: " << exo.error_string() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    //exo.test();

    const auto names = exo.variable_names().value();
    //for (const auto& name : names)
    //    std::cout << name << "\n";

    const auto info = exo.get_variable_info(names[0]);
    if (info.good())
    {
        std::cout << names[0] << "\n";
        std::cout << "Dimensions:    " << info.value().dimensions << "\n";
        std::cout << "Attributes:    " << info.value().attributes << "\n";
        std::cout << "Type:          " << *info.value().type << "\n";
        
        if (info.value().dimension_ids.size())
        {
            std::cout << "Dimension IDS: ";
            for (const auto& id : info.value().dimension_ids)
                std::cout << id << " ";
            std::cout << "\n";
        }
        
    }

    const auto inq = exo.inquire();

    std::cout << "File info:\nndims: " << inq.value().dimensions << "\nnvars: " << inq.value().variables << "\nngatt: " << inq.value().attributes << "\n";

    exo.close();
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

    //return exodus_file();
    return pncdf_file();

    MPI_Finalize();
}