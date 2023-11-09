#include "distributor.hh"

#include "type.hh"

#include <iostream>
#include <algorithm>
#include <cassert>
#include <limits>

namespace pio::io
{
    distributor::subvolume distributor::subvolume::split()
    {
        const auto max_dim_size = std::max_element(counts.begin(), counts.end());
        const auto index = std::distance(counts.begin(), max_dim_size);

        const auto new_size = *max_dim_size / 2;

        distributor::subvolume other_half(*this);
        other_half.counts[index] = new_size;
        counts[index] = new_size + (*max_dim_size % 2 == 0?0:1);
        offsets[index] += other_half.counts[index];
        return other_half;
    }

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
        const auto first_rank = 0U;
        const auto count = processes();

        // We want to figure out how many cells there are in total...
        const auto total_size = std::accumulate(data_volumes.begin(), data_volumes.end(), 0,
        [](auto val, auto v)
        {
            return val + v.cell_count();
        });

        // ... so that we can figure out about how many cells each process should have
        const auto cells_per_process = total_size / (float)count;

        // Now we step through the entire cell count to find out how many 
        // processes per volume
        std::size_t memory_index = 0, last_volume_location = 0;
        uint32_t current_rank = 0, volume_index = 0;
        std::vector<std::vector<uint32_t>> process_counts(data_volumes.size());
        while (memory_index < total_size)
        {
            const auto next_block_location = cells_per_process * (current_rank + 1);
            const auto next_volume_location = [&]()
            {
                if (volume_index == data_volumes.size() - 1) return total_size;
                return std::accumulate(data_volumes.begin(), data_volumes.begin() + volume_index + 1, 0,
                [](auto val, auto& v)
                {
                    return val + v.cell_count();
                });
            }();
            
            // If the volume ends before the block, increment this volume's process count
            if (next_volume_location <= next_block_location)
            {
                process_counts[volume_index].push_back(current_rank);
                memory_index = next_volume_location;
                last_volume_location = next_volume_location;
                volume_index++;
            }
            // Otherwise, if the block ends before the volume, increment the proccess count
            // but don't change volumes
            else
            {
                process_counts[volume_index].push_back(current_rank);
                memory_index = next_block_location;
                current_rank++;
            }
        }

        std::vector<distributor::subvolume> volumes;

        volume_index = 0;
        for (const auto& volume_ranks : process_counts)
        {
            const auto& dimensions = data_volumes[volume_index].dimensions;
            const auto it = std::find(volume_ranks.begin(), volume_ranks.end(), rank());
            if (it != volume_ranks.end())
            {
                if (volume_ranks.size() == 1)
                {
                    volumes.push_back([&](){
                        distributor::subvolume sub_vol;
                        for (const auto& dim : dimensions)
                            sub_vol.counts.push_back(dim);
                        sub_vol.offsets = std::vector<MPI_Offset>(dimensions.size(), 0);
                        sub_vol.volume_index = volume_index;
                        return sub_vol;
                    }());
                }
                else if (volume_ranks.size())
                {
                    std::vector<distributor::subvolume> volume;
                    volume.push_back([&](){
                        distributor::subvolume vol;
                        for (const auto& dim : dimensions)
                            vol.counts.push_back(dim);
                        vol.offsets = std::vector<MPI_Offset>(dimensions.size(), 0);
                        vol.volume_index = volume_index;
                        return vol;
                    }());

                    // Subdivide this volume 
                    for (uint32_t i = 0; i < volume_ranks.size() - 1; i++)
                    {
                        auto [max_cell_count, max_index] = std::pair(
                            std::numeric_limits<uint32_t>::min(),
                            -1
                        );

                        for (uint32_t i = 0; i < volume.size(); i++)
                        {
                            const auto cell_count = std::accumulate(
                                volume[i].counts.begin(), 
                                volume[i].counts.end(), 
                                1, 
                                std::multiplies<std::size_t>()
                            );
                            if (cell_count > max_cell_count) 
                            {
                                max_cell_count = cell_count;
                                max_index = i;
                            }
                        }

                        volume.push_back(volume[max_index].split());
                    }

                    assert(volume.size() == volume_ranks.size());
                    for (auto i = 0; i < volume_ranks.size(); i++)
                        if (volume_ranks[i] == rank())
                            volumes.push_back(volume[i]);
                }
            }
            volume_index++;
        }

        return { std::move(volumes) };
    }   
}