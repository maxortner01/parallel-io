#pragma once

#include <type_traits>
#include <vector>

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
    
    struct Block
    {
        std::string type;
        int id, elements, nodes_per_elem, attributes;
    };
    
    namespace impl
    {
        
    template<typename _Word, io::access _Access>
    struct file_base
    {   
        file_base(const std::string& filename)
        {
            int comp_ws = sizeof(_Word);
            int io_ws = sizeof(_Word);
            float version;
            handle = ex_open(filename.c_str(), (_Access == io::access::ro?EX_READ:EX_WRITE) | EX_LARGE_MODEL, &comp_ws, &io_ws, &version); 

            if constexpr (_Access == io::access::wo)
                if (handle < 0) handle = ex_create(filename.c_str(), EX_WRITE, &comp_ws, &io_ws);

            _good = (handle < 0?false:true);
        }

        file_base(const file_base&) = delete;

        ~file_base()
        { close(); }

        void close()
        {
            if (_good) ex_close(handle);
        }

        auto good() const     { return _good; }
        operator bool() const { return good(); }

    protected:
        int handle, err;
        bool _good;
    };

    template<typename _Word, io::access _Access>
    struct file_access 
    { };

    template<typename _Word>
    struct file_access<_Word, io::access::wo> : file_base<_Word, io::access::wo>
    {
        using base = file_base<_Word, io::access::wo>;
        using base::file_base;

        bool
        set_init_params(const info& info)
        {
            auto err = ex_put_init(this->handle, info.title.c_str(), info.num_dim, info.num_nodes, info.num_elem, info.num_elem_blk, info.num_node_sets, info.num_side_sets);
            if (err < 0) { std::cout << err << " " << ex_strerror(err) << "\n"; return false; }

            return true;
        }

        bool 
        write_time_step(_Word value)
        {
            auto err = ex_put_time(this->handle, time_steps++, &value);
            if (err < 0) { std::cout << err << " " << ex_strerror(err) << "\n"; return false; }
            return true;
        }

        bool
        create_block(const Block& block)
        {
            auto err = ex_put_block(
                this->handle, 
                EX_ELEM_BLOCK, 
                block_counter++, 
                block.type.c_str(),
                block.elements,
                block.nodes_per_elem,
                0,
                0,
                block.attributes
            );
            if (err < 0) return false;
            return true;
        }

    private:
        int time_steps = 1;
        int block_counter = 1;
    };

    template<typename _Word>
    struct file_access<_Word, io::access::ro> : file_base<_Word, io::access::ro>
    {
        using base = file_base<_Word, io::access::ro>;
        using base::file_base;

        io::result<coordinates<_Word>>
        get_node_coordinates() const
        {
            const auto info = get_info();
            
            if (!info.good()) return { info.error() };

            coordinates<_Word> c;
            
            if (info.value().num_dim >= 1) c.x.resize(info.value().num_nodes);
            if (info.value().num_dim >= 2) c.y.resize(info.value().num_nodes);
            if (info.value().num_dim == 3) c.z.resize(info.value().num_nodes);

            const auto error = ex_get_coord(this->handle, &c.x[0], &c.y[0], &c.z[0]);

            if (error < 0) return { error };
            return { c };
        }

        io::result<info> 
        get_info() const
        {
            info i;
            char title[MAX_LINE_LENGTH];
            const auto ret = ex_get_init(
                this->handle, 
                title, 
                &i.num_dim, 
                &i.num_nodes, 
                &i.num_elem, 
                &i.num_elem_blk, 
                &i.num_node_sets, 
                &i.num_side_sets
            );

            if (ret < 0) return { ret };

            i.title = std::string(title);
            return { i };
        }

        auto
        get_time_value_count() const
        {
            return ex_inquire_int(this->handle, EX_INQ_TIME);
        }

        io::result<std::vector<_Word>>
        get_time_values() const
        {
            const auto num_values = get_time_value_count();
            if (num_values < 0) return { num_values };

            if (!num_values) return { std::vector<_Word>() };

            std::vector<_Word> time_values(num_values);
            const auto ret = ex_get_all_times(this->handle, &time_values[0]);
            if (ret < 0) return { ret };
            
            return { time_values };
        }

        io::result<std::vector<std::string>>
        get_variable_names(const scope& _scope) const
        {
            const char* c = (_scope == scope::element?"e":(_scope == scope::global?"g":"n"));
            int num_vars;
            auto error = ex_get_var_param(
                this->handle,
                c,
                &num_vars
            );

            if (error < 0) return { error };

            std::vector<std::string> variable_names(num_vars);
            if (!num_vars) return { variable_names };

            for (auto& v : variable_names) v.resize(MAX_STR_LENGTH + 1);

            error = ex_get_var_names(this->handle, c, num_vars, (char**)&variable_names[0]);

            if (error < 0) return { error };

            return { variable_names };
        }

        io::result<std::vector<Block>>
        get_blocks()
        {
            const auto res = get_info();
            if (!res.good()) return { res.error() };

            std::vector<int> ids(res.value().num_elem_blk);
            auto err = ex_get_ids(this->handle, EX_ELEM_BLOCK, ids.data());

            std::vector<Block> blocks(ids.size());
            for (uint32_t i = 0; i < ids.size(); i++)
            {
                ex_block block{0};
                char type[MAX_STR_LENGTH];
                err = ex_get_block(
                    this->handle, 
                    EX_ELEM_BLOCK, 
                    ids[i],
                    type,
                    &blocks[i].elements,
                    &blocks[i].nodes_per_elem,
                    &block.num_edges_per_entry,
                    &block.num_faces_per_entry,
                    &blocks[i].attributes
                );
                if (err < 0) return { err };

                blocks[i].id = ids[i];
                blocks[i].type = std::string(type);
            }
            return { blocks };
        };

        io::result<std::vector<_Word>>
        get_block_data(const std::string& name, uint32_t time, const Block& block)
        {
            const auto var_names = get_variable_names(scope::element);
            if (!var_names.good()) return { var_names.error() };

            const auto it = std::find(var_names.value().begin(), var_names.value().end(), name);
            if (it == var_names.value().end()) return { -1 };

            std::vector<_Word> res(block.elements);

            const auto index = std::distance(var_names.value().begin(), it);
            const auto err = ex_get_var(this->handle, time, EX_ELEM_BLOCK, index, block.id, block.elements, res.data());
            if (err < 0) return { err };

            return { res };
        }

        // Make block struct that contains elem block parameters meta-data
        // as well as a vector of _Word's that is the data, then the return type
        // will be io::result<std::vector<Block>>

        // This is where we'll need to utilize parallel io, this is where the bulk of 
        // the data comes from
        io::result<std::vector<std::vector<_Word>>>
        get_element_variable_values(const uint32_t time_step, const uint32_t var_ind) const
        {
            const auto time_step_count = get_time_value_count();
            if (time_step_count < 0) return { time_step_count };
            if (time_step > time_step_count || time_step == 0 || var_ind == 0) return { -2 };

            const auto res = get_info();

            std::vector<std::vector<_Word>> ret;
            if (res.good())
            {
                ret.resize(res.value().num_elem_blk);

                std::vector<int> ids(res.value().num_elem_blk);
                auto error = ex_get_elem_blk_ids(this->handle, ids.data());

                for (uint32_t i = 0; i < res.value().num_elem_blk; i++)
                {
                    char elem_type[MAX_STR_LENGTH];
                    int num_elem_this_blk, num_nodes_per_elem, num_attr;

                    error = ex_get_elem_block(this->handle, ids[i], elem_type, &num_elem_this_blk, &num_nodes_per_elem, &num_attr);
                    if (error < 0) return { error };

                    ret[i].resize(num_elem_this_blk);
                    error = ex_get_elem_var(this->handle, time_step, var_ind, ids[i], num_elem_this_blk, ret[i].data());
                    if (error < 0) return { error };
                }

                return { ret };
            }

            return { res.error() };
        }
    };

    } // namespace impl

    // User should use this. When access methods include rw as well, inherit from both read and write
    // file_access structs
    template<
        typename _Word,
        io::access _Access,
        typename = std::enable_if_t<sizeof(_Word) == 4 || sizeof(_Word) == 8>>
    struct file : impl::file_access<_Word, _Access>
    { using impl::file_access<_Word, _Access>::file_access; };
}