#pragma once

#include "../external.hh"

#include "result.hh"
#include "type.hh"

#include <vector>
#include <numeric>

namespace pio::io
{
    struct distributor
    {
        struct volume
        {
            uint32_t data_index;
            nc_type  data_type; // possibly replace with an enum
            std::vector<std::size_t> dimensions;

            std::size_t cell_count() const
            {
                return std::accumulate(
                    dimensions.begin(), 
                    dimensions.end(), 
                    1, 
                    std::multiplies<std::size_t>()
                );
            }

            std::size_t byte_size() const
            {
                return cell_count() * nc_sizeof(data_type);
            }
        };

        struct subvolume
        {
            uint32_t volume_index;
            std::vector<MPI_Offset> offsets;
            std::vector<MPI_Offset> counts;

            subvolume split();
        };

        std::vector<volume> data_volumes;

        distributor(MPI_Comm communicator);

        /**
         * @brief Given the mpi comm and the list data_volumes, attempts to evenly distribute data load.
         * @return io::result<std::vector<subvolume>> A list of subvolumes this process is responsible for.
         */
        io::result<std::vector<subvolume>>
        get_tasks() const;

        auto rank() const { return _rank; }
        auto processes() const { return _processes; }

    private:
        const int _rank, _processes;
    };
}