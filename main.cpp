#include "./pio/pio.hh"

#include <iostream>
#include <cassert>

#define ASSERT(expr, msg) if (!(expr)) { std::cout << msg << "\n"; assert(expr); }
using namespace pio;
using word_t = unsigned long;

void check(const exodus::result<void>& res)
{
    ASSERT((bool)res, "Error writing to exodus file: " << res.error().message());
}

template<typename W>
void
write_coloring(
    const std::string& name,
    const uint32_t& dim,
    const std::vector<exodus::block<W>>& blocks,
    const std::vector<std::size_t>& colors)
{
    const auto total_elem = std::accumulate(blocks.begin(), blocks.end(), 0, [](std::size_t a, const exodus::block<W>& block) { return a + block.info.elements; });
    assert(colors.size() == total_elem);

    const auto colorings = [&]() -> std::vector<exodus::real<W>>
    {
        std::vector<exodus::real<W>> r(colors.size());
        for (std::size_t i = 0; i < colors.size(); i++)
            r[i] = static_cast<exodus::real<W>>(colors[i]);
        return r;
    }();

    constexpr nc_type TYPE = (sizeof(W) == 8 ? types::Double::nc : types::Float::nc);

    //exodus::file<W, io::access::rw> file(name);
    
    // here's where we read/write coordinate information !!!

    // then we write the block info
    io::distributor dist(MPI_COMM_WORLD);

    uint32_t index = 0;
    for (const auto& block : blocks)
    {
        io::distributor::volume vol;
        vol.data_index = index++;
        vol.data_type = TYPE;
        vol.dimensions.push_back(1);
        vol.dimensions.push_back(block.info.elements);
        dist.data_volumes.push_back(vol);
    }

    const auto vols = [&]()
    {
        const auto res = dist.get_tasks();
        assert(res);
        return std::move(res.value());
    }();

    netcdf::file<io::access::rw> file(name);
    assert(file);

    const auto res = file.define([&]() -> netcdf::result<void>
    {
        for (const auto& block : blocks)
        {
            const auto res = file.define_variable<io::type<TYPE>>("vals_elem_var1eb" + std::to_string(block.info.id), { "time_step", "num_el_in_blk" + std::to_string(block.info.id) });
            if (!res) return { res.error() };
        }
        return { };
    });

    if (!res)
    {
        std::cout << "error creating var: " << res.error().message() << "\n";
        assert(res);
    }

    using promise = netcdf::promise<io::access::wo, io::type<TYPE>>;
    std::vector<promise> promises;
    for (const auto& vol : vols)
    {
        const auto& block = blocks[vol.volume_index];

        auto index = 0U;
        for (uint32_t i = 0; i < vol.volume_index; i++)
            index += blocks[i].info.elements;

        const auto p = file.write_variable<io::type<TYPE>>("vals_elem_var1eb" + std::to_string(block.info.id), &colorings[index + vol.offsets[1]], vol.counts[1], vol.offsets, vol.counts);
        if (!p)
        {
            std::cout << "error making promise: " << p.error().message() << "\n";
            assert(p);
        }

        promises.push_back(p);
    }

    for (const auto& promise : promises)
        const auto vals = promise.wait();
}

template<typename W, io::access Access>
void
write_block(
    exodus::file<W, Access>& file,
    typename exodus::block<W>::header block,
    const std::vector<int>& entity_nodes,
    const std::vector<int>& entity_node_counts)
{
    block.name = "cell-block-" + std::to_string(block.id);
    check(file.create_block(block));
    check(file.set_block_connectivity(block, entity_nodes.data(), entity_nodes.size()));
    if (block.type == "nsided")
        check(file.set_entity_count_per_node(block, entity_node_counts.data(), entity_node_counts.size()));
}

int run()
{
    std::vector<std::size_t> colors;

    const std::string mesh_file = "../box-hex.exo";
    exodus::file<word_t, io::access::ro> md(mesh_file);
    const auto md_info = [&]()
    {
        const auto res = md.get_info();
        assert(res);
        return std::move(res.value());
    }();

    const std::string output = [&]()
    {
        auto pos = mesh_file.find_last_of('.');
        ASSERT(pos != std::string::npos, "invalid name");
        return mesh_file.substr(0, pos) + "-colors.exo";
    }();

    io::distributor dist(MPI_COMM_WORLD);
    if (!dist.rank())
    {
        exodus::file<word_t, io::access::wo> file(output, true);
        if (!file) { std::cout << "Error creating file: \"" << file.error().message() << "\"\n"; return -1; }
        const auto res = file.set_init_params(md_info);
        if (!res) { std::cout << "Error setting init: " << res.error().message() << "\n"; return -1; }

        const auto block_res = md.get_blocks();
        assert(block_res);
        const auto& blocks = block_res.value();

        check(file.set_variable_count(exodus::scope::element, 1));
        check(file.set_variable_name(exodus::scope::element, "color")); // variable index 1
        check(file.write_time_step(0));

        // set the block information
        for (const auto& block : blocks)
        {
            const auto conn_res = md.get_block_connectivity(block.info);
            assert(conn_res);
            const auto& conn = conn_res.value();
            
            const auto poly = md.get_entity_count_per_node(block.info);

            write_block(file, block.info, conn, ( poly ? poly.value() : std::vector<int>() ));
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    const auto blocks = [&]()
    { 
        exodus::file<word_t, io::access::ro> file(output);
        const auto block_res = file.get_blocks();
        assert(block_res);
        return std::move(block_res.value());
    }();

    for (const auto& block : blocks)
        for (uint32_t i = 0; i < block.info.elements; i++)
            colors.push_back(colors.size() + 1);

    write_coloring<word_t>(output, md_info.num_dim, blocks, colors);

    return 0;
}

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    const auto val = run();

    MPI_Finalize();

    return val;
}