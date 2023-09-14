#pragma once

#include <exodusII.h>
#include <type_traits>
#include <vector>

#include "../io.hh"

namespace pio::exodus
{
    enum class scope
    {
        global, node, element
    };

    namespace impl
    {
        
    template<typename _Word, io::access _Access>
    struct file_base
    {   
        struct coordinates
        {
            std::vector<_Word> x, y, z;
        };

        struct info
        {
            std::string title;
            int num_dim, num_nodes, num_elem, num_elem_blk, num_node_sets, num_side_sets;
        };

        file_base(const std::string& filename)
        {
            int comp_ws = sizeof(_Word);
            int io_ws = 0;
            float version;
            handle = ex_open(filename.c_str(), (_Access == io::access::ro?EX_READ:EX_WRITE), &comp_ws, &io_ws, &version); 

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
    struct file_access<_Word, io::access::ro> : file_base<_Word, io::access::ro>
    {
        using base = file_base<_Word, io::access::ro>;

        using base::file_base;

        io::result<typename base::coordinates>
        get_node_coordinates() const
        {
            const auto info = get_info();
            
            if (!info.good()) return { info.error() };

            typename base::coordinates c;
            
            if (info.value().num_dim >= 1) c.x.resize(info.value().num_nodes);
            if (info.value().num_dim >= 2) c.y.resize(info.value().num_nodes);
            if (info.value().num_dim == 3) c.z.resize(info.value().num_nodes);

            const auto error = ex_get_coord(this->handle, &c.x[0], &c.y[0], &c.z[0]);

            if (error < 0) return { error };
            return { c };
        }

        io::result<typename base::info> 
        get_info() const
        {
            typename base::info i;
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