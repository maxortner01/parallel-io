#pragma once

#include "../external.hh"

#include "result.hh"
#include "type.hh"

#include <vector>
#include <numeric>

namespace pio::io
{
    /// Handles distributing data volumes evenly across given MPI execution space
    struct distributor
    {
        /// Volume of data to distribute
        struct volume
        {
            uint32_t data_index; /// Index into user's list of their volume \note the user should have a list of the data representation (for example a list of strings, or a list of the data contents themselves) and set the data_index value to the index in this list the corresponding volume is at
            nc_type  data_type;  /// The type of the data inside the volume
            std::vector<std::size_t> dimensions; /// The size of each dimension in this volume

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

        /// A sub-volume of a particular \ref volume found inside data_volumes
        struct subvolume
        {
            uint32_t volume_index; /// The index into data_volumes to which this subvolume belongs
            std::vector<MPI_Offset> offsets; /// The starting point of this subvolume
            std::vector<MPI_Offset> counts;  /// The dimensions of this subvolume

            subvolume split();
        };

        /// List of volumes to split among processes
        std::vector<volume> data_volumes;

        distributor(MPI_Comm communicator);

        /// Given the mpi comm and the list data_volumes, attempts to evenly distribute data load.
        /// @return io::result<std::vector<subvolume>> A list of subvolumes this process is responsible for.
        io::result<std::vector<subvolume>>
        get_tasks() const;

        auto rank() const { return _rank; }
        auto processes() const { return _processes; }

    private:
        const int _rank, _processes;
    };
}