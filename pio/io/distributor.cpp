#include "distributor.hh"

#include "type.hh"

#include <iostream>
#include <algorithm>
#include <cassert>

namespace pio::io
{
    distributor::distributor(MPI_Comm communicator) :
        _rank([](auto comm) -> auto
        {
            int init;
            MPI_Initialized(&init);
            assert(init);

            int rank;
            MPI_Comm_rank(comm, &rank);
            return rank;
        }(communicator)),
        _processes([](auto comm) -> auto
        {
            int init;
            MPI_Initialized(&init);
            assert(init);

            int procs;
            MPI_Comm_size(comm, &procs);
            return procs;
        }(communicator))
    {

    }

    io::result<std::vector<distributor::subvolume>>
    distributor::get_tasks() const
    {
        auto sorted_volumes = data_volumes;
        // if product of counts vanishes, don't add to return
        std::sort(sorted_volumes.begin(), sorted_volumes.end(), 
        [](const auto& a, const auto& b) 
        {
            return a.cell_count() * nc_sizeof(a.data_type) < b.cell_count() * nc_sizeof(b.data_type);
        });

        std::remove_if(sorted_volumes.begin(), sorted_volumes.end(),
        [](const auto& a)
        {
            for (const auto& dim : a.dimensions)
                if (!dim) return true;
            return false;
        });

        const auto total_size = std::accumulate(sorted_volumes.begin(), sorted_volumes.end(), 0,
        [](auto val, auto v)
        {
            return val + v.cell_count(); // * nc_sizeof(v.data_type);
        });

        std::vector<distributor::subvolume> volumes;

        std::size_t last_volume = 0;
        uint32_t volume_index = 0;
        uint32_t current_rank = 0;
        std::size_t memory_index = 0;
        const auto amount = total_size / (std::size_t)processes();
        while (memory_index < total_size)
        {
            const auto next_block = amount * (current_rank + 1);

            const auto next_volume = [&]()
            {
                if (volume_index == sorted_volumes.size() - 1) return total_size;
                return std::accumulate(sorted_volumes.begin(), sorted_volumes.begin() + volume_index + 1, 0,
                [](auto val, auto& v)
                {
                    return val + v.cell_count();
                });
            }();

            if (next_volume < next_block)
            {
                if (current_rank == rank())
                {
                    distributor::subvolume sub_vol;
                    sub_vol.volume_index = sorted_volumes[volume_index].data_index;
                    
                    // we want to generate the offsets and counts for 
                    // memory_index - last_volume to next_volume - last_volume - 1
                    
                    volumes.push_back(sub_vol);
                    std::cout << "(" << current_rank << ") from " << memory_index - last_volume << " to " << next_volume - last_volume - 1 << " in volume " << sorted_volumes[volume_index].data_index << "\n";
                }
                memory_index = next_volume;
                last_volume = next_volume;
                volume_index++;
            }
            else
            {
                if (current_rank == rank())
                {
                    distributor::subvolume sub_vol;
                    sub_vol.volume_index = sorted_volumes[volume_index].data_index;

                    // we want to generate the offsets and counts for 
                    // memory_index - last_volume to next_block - last_volume - 1

                    volumes.push_back(sub_vol);
                    std::cout << "(" << current_rank << ") from " << memory_index - last_volume << " to " << next_block - last_volume - 1 << " in volume " << sorted_volumes[volume_index].data_index << "\n";
                }
                memory_index = next_block;
                current_rank++;
            }
        }

        return { volumes };


        /*
        amount = total_size / processes() is the amount of bytes per process
        Begin stepping through the total data
        When the edge of a data_volume is hit, or amount is hit generate a subvolume
        When an edge is hit: set the total size to be total_size - size of this volume
          then continue stepping
        */
    }   
}