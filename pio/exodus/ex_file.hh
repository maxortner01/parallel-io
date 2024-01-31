#pragma once

#include <type_traits>
#include <vector>
#include <optional>
#include <unordered_map>

#include "../io.hh"

namespace pio::exodus
{
    // need to expand this to include other options
    enum class scope
    {
        global, node, element
    };

    template<typename _Word>
    using integer = std::conditional_t<sizeof(_Word) == 8, int64_t, int32_t>;
    
    template<typename _Word>
    using real = std::conditional_t<sizeof(_Word) == 8, double, float>;
    
    template<typename _Word>
    struct coordinates
    {
        std::vector<_Word> x, y, z;
    };

    template<typename _Word>
    struct info
    {
        std::string title;
        integer<_Word> num_dim = 0, num_nodes = 0, num_elem = 0, num_elem_blk = 0, num_node_sets = 0, num_side_sets = 0;
    };
    
    template<typename _Word>
    struct block
    {
        struct header
        {
            std::string type, name;
            integer<_Word> id, elements, nodes_per_elem, attributes, edges_per_entry, faces_per_entry;
        } info;
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
            VariableIndexOutOfBounds,
            WrongConnectivityDimensions,
            WrongNodeSize,
            WrongBlockType,
            VariableCountNotSet,
            VariableCountAlreadySet,
            ScopeNotSupported
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
        error_code(int c, const std::string& func_name = "");

        /// Convert the error to string
        std::string message() const;

    private:
        union
        {
            code _code;
            int  _exodus;
        };
        std::optional<std::string> _func_name;
        bool _exodus_error;

        static std::string _to_string(code c);
    };
    
    static error_code exodus_error(int num, const std::string& func_name = "")
    {
        return error_code(num, func_name);
    }
    
    template<typename T>
    using result = io::result<T, error_code>;

    template<typename _Word, io::access _Access>
    struct file
    {
        inline static constexpr bool Is64 = ( sizeof(_Word) == 8 );

        using block_header = typename block<_Word>::header;

        file(const std::string& filename, bool overwrite = false);
        file(file&&);
        
        file(const file&) = delete;

        ~file();

        void close();
        auto good() const     { return _good; }
        operator bool() const { return good(); }
        const error_code error() const { return exodus_error(_err); }

        /* WRITE/READ-WRITE */
        WRITE result<void> 
        set_init_params(const info<_Word>& info);

        WRITE result<void>
        write_time_step(real<_Word> value);

        WRITE result<void>
        create_block(const block_header& block);

        WRITE result<void>
        set_variable_count(scope s, uint32_t count);

        WRITE result<void>
        set_variable_names(scope s, const std::vector<std::string>& names);

        WRITE result<void>
        set_variable_name(scope s, const std::string& name);

        // strongly recommended the count is passed in if using
        // dynamically allocated arrays, otherwise you risk an overflow
        // and possible seg fault if the array size is not correct
        WRITE result<void>
        set_block_connectivity(const block_header& block, const int* connect, std::optional<std::size_t> count = std::nullopt);

        WRITE result<void>
        set_entity_count_per_node(const block_header& block, const int* connect, std::optional<std::size_t> count  = std::nullopt);

        /* READ/READ-WRITE */
        READ result<info<_Word>>
        get_info() const;

        READ result<std::vector<int>>
        get_block_connectivity(const block_header& block) const;
        
        READ result<std::vector<int>>
        get_entity_count_per_node(const block_header& block) const;

        READ result<coordinates<_Word>>
        get_node_coordinates() const;

        READ result<std::vector<_Word>>
        get_time_values() const;

        READ result<std::vector<std::string>>
        get_variable_names(const scope& _scope) const;

        READ result<std::vector<block<_Word>>>
        get_blocks() const;

        READ result<void>
        get_block_data(const std::string& name, integer<_Word> time_step, block<_Word>& block) const;

        READ result<std::vector<std::vector<_Word>>>
        get_element_variable_values(const integer<_Word> time_step, const integer<_Word> var_ind) const;

        int _handle, _err;
    private:
        bool _good;
        integer<_Word> _time_steps = -1, _block_counter = 1;
        std::unordered_map<scope, uint32_t> variable_counts;
    };
}