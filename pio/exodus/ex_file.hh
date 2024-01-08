#pragma once

#include <type_traits>
#include <vector>
#include <optional>
#include <unordered_map>

#include "../io.hh"

namespace pio::exodus
{
    enum class scope
    {
        global, node, element
    };

    template<typename _Word>
    struct coordinates
    {
        std::vector<_Word> x, y, z;
    };

    struct info
    {
        std::string title;
        std::uint64_t num_dim = 0, num_nodes = 0, num_elem = 0, num_elem_blk = 0, num_node_sets = 0, num_side_sets = 0;
    };
    
    template<typename _Word>
    struct Block
    {
        std::string type;
        int id, elements, nodes_per_elem, attributes;
        std::optional<std::unordered_map<std::string, std::vector<_Word>>> data; // change this to span
    };
    
    // add line numbers to errors

    /// Basic storage for errors, contains both PIO errors and pnetcdf errors
    struct error_code
    {
        enum code
        {
            FileNotGood,
            InquireError,
            VarNotPresent,
            TimeStepError,
            TimeStepNotPresent,
            TimeStepIndexOutOfBounds,
            VariableIndexOutOfBounds
        };

        /**
         * @brief Initialize with a PIO error
         * @param c PIO error
         */
        error_code(code c);

        /**
         * @brief Initialize with exodus error
         * @param c 
         */
        error_code(int c);

        /// Convert the error to string
        std::string message() const;

    private:
        union
        {
            code _code;
            int  _exodus;
        };
        bool _exodus_error;

        static std::string _to_string(code c);
    };
    
    static error_code exodus_error(int num)
    {
        return error_code(num);
    }
    
    template<typename T>
    using result = io::result<T, error_code>;

    template<typename _Word, io::access _Access>
    struct file
    {
        file(const std::string& filename);
        file(file&&);
        
        file(const file&) = delete;

        ~file();

        void close();
        auto good() const     { return _good; }
        operator bool() const { return good(); }

        /* WRITE/READ-WRITE */
        WRITE result<void> 
        set_init_params(const info& info);

        WRITE result<void>
        write_time_step(_Word value);

        WRITE result<void>
        create_block(const Block<_Word>& block);

        /* READ/READ-WRITE */
        READ result<info>
        get_info() const;

        READ result<coordinates<_Word>>
        get_node_coordinates() const;

        READ result<std::vector<_Word>>
        get_time_values() const;

        READ result<std::vector<std::string>>
        get_variable_names(const scope& _scope) const;

        READ result<std::vector<Block<_Word>>>
        get_blocks() const;

        READ result<void>
        get_block_data(const std::string& name, uint32_t time_step, Block<_Word>& block) const;

        READ result<std::vector<std::vector<_Word>>>
        get_element_variable_values(const uint32_t time_step, const uint32_t var_ind) const;

    private:
        int _handle, _err;
        bool _good;
        uint32_t _time_steps = 1, _block_counter = 1;
    };
}