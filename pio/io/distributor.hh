#pragma once

#include "../external.hh"

#include "result.hh"
#include "type.hh"

#include <vector>
#include <numeric>

namespace pio::io
{
    /** \brief Handles distributing data volumes evenly across given MPI execution space
     * 
     * \todo When the MPI process count exeeds the total amount of cells, we get size errors. We can do two things: either limit the amount
     * of proccessors when we perform the computation <i>or</i> we can check all the subvolumes we generated and erase the ones that have 
     * a vanishing internal volume (easiest).
     * 
     * Basic usage of this struct entails (firstly) creating it with an MPI execution space
     * \code {.cpp}
     * io::distributor dist(MPI_COMM_WORLD); 
     * \endcode
     * Then, create your list of volumes. For example, if you want to use this to extract variable data from a file
     * your list can be a list of strings with the corresponding types
     * \code {.cpp}
     * const std::vector<std::string> names = { "var1", "var2", ... };
     * \endcode
     * Then create a volume for each name
     * \code {.cpp}
     * for (uint32_t i = 0; i < names.size(); i++)
     * {
     *     io::distributor::volume vol;
     *     vol.data_index = i;
     *     // set vol.data_type to the corresponding type of the i-th variable
     *     // push each dimension size to vol.dimensions
     *     dist.data_volumes.push_back(vol);
     * }
     * \endcode
     * Then, the distributor is ready to go:
     * \code {.cpp}
     * const std::vector<io::distributor::subvolume> subvols = dist.split();
     * for (const auto& subvol : subvols)
     * {
     *     // This is rather verbose, but gives the correct functionality
     *     const auto& variable_name = names[dist.data_volumes[subvol.volume_index].data_index];
     *     // now you can perform a read or a write with this information, something like:
     *     // file.write_variable<io::type::Float::nc>(variable_name, &data[0], data.size(), subvol.offsets, subvol.counts);
     * }
     * \endcode
     */
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

        /// A sub-volume of a particular \ref volume found inside `data_volumes`
        struct subvolume
        {
            uint32_t volume_index; /// The index into `data_volumes` to which this subvolume belongs
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