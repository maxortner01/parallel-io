#include "ex_file.hh"

#include "../external.hh"

#define FWD_DEC_WRITE_O(word, ret, name, acc, ...) template ret file<word, io::access::acc>::name(__VA_ARGS__)
#define FWD_DEC_READ_O(word, ret, name, access, ...) FWD_DEC_WRITE_O(word, ret, name, access, __VA_ARGS__) const

#define FWD_DEC_WRITE(word, ret, name, ...) FWD_DEC_WRITE_O(word, ret, name, wo, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, rw, __VA_ARGS__)
#define FWD_DEC_READ(word, ret, name, ...) FWD_DEC_READ_O(word, ret, name, ro, __VA_ARGS__); FWD_DEC_READ_O(word, ret, name, rw, __VA_ARGS__)
#define FWD_DEC_ALL(word, ret, name, ...) FWD_DEC_WRITE_O(word, ret, name, ro, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, rw, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, wo, __VA_ARGS__)

#define EXO_CHECK(val) { auto err = val; if (err < 0) return { exodus_error(err, #val) }; }

namespace pio::exodus
{

error_code::error_code(code c) :
    _code(c),
    _exodus_error(false)
{   }

error_code::error_code(int c, const std::string& func_name) :
    _exodus(c),
    _exodus_error(true)
{   
    if (func_name.size()) _func_name = func_name;
}

std::string
error_code::message() const
{
    switch(_exodus_error)
    {
    case true:  return "Exodus error (" + (_func_name ? (*_func_name + " returned ") : "") + std::to_string(_exodus) + "): " + std::string(ex_strerror(_exodus));
    case false: return "PIO error:" + _to_string(_code);
    }
}

std::string
error_code::_to_string(code c)
{
    switch (c)
    {
    case code::VarNotPresent:               return "requested variable name not present";
    case code::InquireError:                return "inquiry error";
    case code::FileNotGood:                 return "file not good";
    case code::TimeStepError:               return "error with time step count";
    case code::TimeStepNotPresent:          return "requested time step does not exist";
    case code::TimeStepIndexOutOfBounds:    return "time step indices start at 1";
    case code::VariableIndexOutOfBounds:    return "variable indices start at 1";
    case code::WrongConnectivityDimensions: return "connectivity should be num_elems_this_blk x num_nodes_per_elem";
    case code::WrongNodeSize:               return "entity counts should be num_elems_this_blk x num_nodes_per_elem";
    case code::WrongBlockType:              return "block type needs to be \"nsided\"";
    case code::VariableCountNotSet:         return "variable count not set for this scope";
    case code::VariableCountAlreadySet:     return "variable count for this scope has already been assigned";
    case code::ScopeNotSupported:           return "given scope type not supported";
    case code::DimensionSizeMismatch:       return "dimension size mismatch";
    default: return "Error error";
    }
}

template<typename _Word, io::access _Access>
file<_Word, _Access>::file(const std::string& filename, bool overwrite) :
    _handle(-1)
{
    int comp_ws = sizeof(_Word);
    int io_ws = sizeof(_Word);
    float version;

    int flags = 0;
    //if constexpr (Is64)
        //flags |= EX_LARGE_MODEL;

    if constexpr (_Access == io::access::ro)
        _handle = ex_open(filename.c_str(), flags | EX_READ, &comp_ws, &io_ws, &version); 
    else
    {
        _handle = ex_create(filename.c_str(), (overwrite ? EX_CLOBBER : EX_NOCLOBBER), &comp_ws, &io_ws);
        if (_handle < 0)
            _handle = ex_open(filename.c_str(), flags | EX_WRITE, &comp_ws, &io_ws, &version);
    }

    _good = (_handle < 0?false:true);
    if (!_good) _err = _handle;
    //else
        //if constexpr (Is64)
            //ex_set_int64_status(_handle, EX_ALL_INT64_DB);

}
FWD_DEC_ALL(unsigned long, , file, const std::string&, bool);

template<typename _Word, io::access _Access>
file<_Word, _Access>::file(file&& f) :
    _handle(f._handle)
{
    f._handle = 0;
}
FWD_DEC_ALL(unsigned long, , file, file&&);

template<typename _Word, io::access _Access>
file<_Word, _Access>::~file()
{
    close();
}
FWD_DEC_ALL(unsigned long, , ~file);

template<typename _Word, io::access _Access>
void file<_Word, _Access>::close()
{
    if (_handle >= 1)
    {
        ex_close(_handle);
        _handle = -1;
    }
}
FWD_DEC_ALL(unsigned long, void, close);

#pragma region WRITE

template<typename _Word, io::access _Access>
template<typename>
result<void> 
file<_Word, _Access>::set_init_params(const info<_Word>& info)
{
    if (!good()) return { error_code::FileNotGood };

    EXO_CHECK(ex_put_init(
        _handle, 
        info.title.c_str(), 
        info.num_dim, 
        info.num_nodes, 
        info.num_elem,
        info.num_elem_blk,
        info.num_node_sets,
        info.num_side_sets
    ));
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_init_params, const info<unsigned long>&);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::write_time_step(real<_Word> value)
{
    if (!good()) return { error_code::FileNotGood };
    if (_time_steps < 0)
    {
        if constexpr (_Access == io::access::wo)
            _time_steps = 1;
        else
        {
            const auto value_count = ex_inquire_int(_handle, EX_INQ_TIME);
            if (value_count < 0) return { exodus_error(value_count) };
            _time_steps = value_count + 1;
        }
    }

    EXO_CHECK(ex_put_time(_handle, _time_steps++, &value));
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, write_time_step, double);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::create_block(
    const typename block<_Word>::header& block)
{
    EXO_CHECK(ex_put_block(
        _handle, 
        EX_ELEM_BLOCK,
        _block_counter++,
        block.type.c_str(),
        block.elements,
        block.nodes_per_elem,
        block.edges_per_entry,
        block.faces_per_entry,
        block.attributes
    ));

    if (block.name.size())
        EXO_CHECK(ex_put_name(_handle, EX_ELEM_BLOCK, _block_counter - 1, block.name.c_str()));

    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, create_block, const typename block<unsigned long>::header&);

static std::optional<ex_entity_type> from_scope(scope s)
{
    switch (s)
    {
    case scope::element: return EX_ELEM_BLOCK;
    case scope::node:    return EX_NODAL;
    case scope::global:  return EX_GLOBAL;
    default: return std::nullopt;
    }
}

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_variable_count(
    scope s, 
    uint32_t count)
{
    if (!good()) return { error_code::FileNotGood };

    auto ex_s = from_scope(s);
    if (!ex_s) return { error_code::ScopeNotSupported };

    if (!variable_counts.count(s) && _Access  == io::access::rw)
    {
        // check the file
        int num_vars = 0;
        EXO_CHECK(ex_get_variable_param(_handle, *ex_s, &num_vars));
        if (num_vars) variable_counts.insert(std::pair(s, num_vars));
    }

    if (variable_counts.count(s))
        return { error_code::VariableCountAlreadySet };

    EXO_CHECK(ex_put_variable_param(_handle, *ex_s, count));
    variable_counts.insert(std::pair(s, count));
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_variable_count, scope, uint32_t);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_variable_names(
    scope s, 
    const std::vector<std::string>& names)
{
    if (!good()) return { error_code::FileNotGood };

    auto ex_s = from_scope(s);
    if (!ex_s) return { error_code::ScopeNotSupported };

    if (!variable_counts.count(s) && _Access  == io::access::rw)
    {
        // check the file
        int num_vars = 0;
        EXO_CHECK(ex_get_variable_param(_handle, *ex_s, &num_vars));
        if (num_vars) variable_counts.insert(std::pair(s, num_vars));
    }

    if (!variable_counts.count(s))
        return { error_code::VariableCountNotSet };
    
    std::vector<char*> _names;
    _names.reserve(names.size());
    for (const auto& name : names)
        _names.push_back(const_cast<char*>(name.c_str()));
    EXO_CHECK(ex_put_variable_names(_handle, *ex_s, _names.size(), _names.data()));
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_variable_names, scope, const std::vector<std::string>&);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_variable_name(
    scope s, 
    const std::string& name)
{
    const std::vector<std::string> names = { name };
    return set_variable_names(s, names);
}
FWD_DEC_WRITE(unsigned long, result<void>, set_variable_name, scope, const std::string&);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_block_connectivity(
    const typename block<_Word>::header& block, 
    const int* connect,
    std::optional<std::size_t> count)
{
    if (count && *count != block.nodes_per_elem * block.elements)
        return { error_code::WrongConnectivityDimensions };
    
    EXO_CHECK(ex_put_conn(
        _handle,
        EX_ELEM_BLOCK,
        block.id,
        connect,
        nullptr, nullptr
    ));

    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_block_connectivity, const typename block<unsigned long>::header&, const int*, std::optional<std::size_t>);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_entity_count_per_node(
    const typename block<_Word>::header& block, 
    const int* connect,
    std::optional<std::size_t> count)
{
    if (block.type != "nsided")
        return { error_code::WrongBlockType };

    if (count && *count != block.nodes_per_elem * block.elements)
        return { error_code::WrongNodeSize };
    
    EXO_CHECK(ex_put_entity_count_per_polyhedra(
        _handle,
        EX_ELEM_BLOCK,
        block.id,
        connect
    ));

    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_entity_count_per_node, const typename block<unsigned long>::header&, const int*, std::optional<std::size_t>);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::set_coordinate_names(
    const std::vector<std::string>& names)
{
    const auto info_res = get_info();
    if (!info_res) return { info_res.error() };

    if (names.size() != info_res.value().num_dim)
        return { error_code::DimensionSizeMismatch };

    std::vector<char*> name_ptr(info_res.value().num_dim);
    for (uint32_t i = 0; i < name_ptr.size(); i++)
    {
        name_ptr[i] = reinterpret_cast<char*>(std::calloc(MAX_NAME_LENGTH, 1));
        strcpy(name_ptr[i], names[i].c_str());
    }
    
    const auto free = [&]()
    {
        for (auto*& name : name_ptr) std::free(name);
    };

    const auto err = ex_put_coord_names(_handle, name_ptr.data());
    free();
    if (err < 0) return exodus_error(err);
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_coordinate_names, const std::vector<std::string>&);

#pragma endregion WRITE

#pragma region READ

template<typename _Word, io::access _Access>
template<typename>
result<info<_Word>>
file<_Word, _Access>::get_info() const
{
    info<_Word> i;
    char title[MAX_LINE_LENGTH];
    EXO_CHECK(ex_get_init(
        _handle,
        title,
        &i.num_dim,
        &i.num_nodes,
        &i.num_elem,
        &i.num_elem_blk,
        &i.num_node_sets,
        &i.num_side_sets
    ));

    i.title = std::string(title);
    return { std::move(i) };
}
FWD_DEC_READ(unsigned long, result<info<unsigned long>>, get_info);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<int>>
file<_Word, _Access>::get_block_connectivity(
    const typename block<_Word>::header& block) const
{
    std::vector<int> conn(block.elements * block.nodes_per_elem);
    EXO_CHECK(ex_get_elem_conn(_handle, block.id, conn.data()));
    return { std::move(conn) };
}
FWD_DEC_READ(unsigned long, result<std::vector<int>>, get_block_connectivity, const typename block<unsigned long>::header&);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<int>>
file<_Word, _Access>::get_entity_count_per_node(
    const typename block<_Word>::header& block) const
{
    if (block.type != "nsided")
        return { error_code::WrongBlockType };
    
    std::vector<int> count(block.nodes_per_elem * block.elements);
    EXO_CHECK(ex_get_entity_count_per_polyhedra(_handle, EX_ELEM_BLOCK, block.id, count.data()));
    return { std::move(count) };
}
FWD_DEC_READ(unsigned long, result<std::vector<int>>, get_entity_count_per_node, const typename block<unsigned long>::header&);

template<typename _Word, io::access _Access>
template<typename>
result<coordinates<_Word>>
file<_Word, _Access>::get_node_coordinates() const
{
    const auto info = get_info();
    if (!info) return { info.error() };

    coordinates<_Word> c;

    const auto& [dim, nodes] = 
        std::make_pair(info.value().num_dim, info.value().num_nodes);
    if (dim >= 1) c.x.resize(nodes);
    if (dim >= 2) c.y.resize(nodes);
    if (dim == 3) c.z.resize(nodes);

    const auto err = ex_get_coord(_handle, &c.x[0], &c.y[0], &c.z[0]);
    if (err < 0) return { exodus_error(err) };
    return { std::move(c) };
}
FWD_DEC_READ(unsigned long, result<coordinates<unsigned long>>, get_node_coordinates);


template<typename _Word, io::access _Access>
template<typename>
result<std::vector<std::string>>
file<_Word, _Access>::get_coordinate_names() const
{
    const auto res = get_info();
    if (!res) return { res.error() };
    if (!res.value().num_dim) return { std::vector<std::string>() };

    const auto& dim = res.value().num_dim;
    char** names = [&]()
    {
        auto** ptr = reinterpret_cast<char**>(std::calloc(dim, sizeof(char*)));
        for (uint32_t i = 0; i < dim; i++)
            ptr[i] = reinterpret_cast<char*>(std::calloc(MAX_NAME_LENGTH, 1));
        return ptr;
    }();

    const auto free = [&]()
    {
        for (uint32_t i = 0; i < dim; i++)
            std::free(names[i]);
        std::free(names);
    };

    const auto err = ex_get_coord_names(_handle, names);
    if (err < 0)
    {
        free();
        return { exodus_error(err) };
    }

    std::vector<std::string> r(dim);
    for (uint32_t i = 0; i < dim; i++)
        for (uint32_t j = 0; j < MAX_NAME_LENGTH; j++)
            if (names[i][j]) r[i] += names[i][j];

    free();
    return { std::move(r) };
}
FWD_DEC_READ(unsigned long, result<std::vector<std::string>>, get_coordinate_names);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<_Word>>
file<_Word, _Access>::get_time_values() const
{
    const auto value_count = ex_inquire_int(_handle, EX_INQ_TIME);
    if (value_count < 0) return { error_code::InquireError };
    if (!value_count)    return { std::vector<_Word>() };

    std::vector<_Word> time_values(value_count);
    const auto err = ex_get_all_times(_handle, &time_values[0]);
    if (err < 0) return { exodus_error(err) };

    return { std::move(time_values) };
}
FWD_DEC_READ(unsigned long, result<std::vector<unsigned long>>, get_time_values);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<std::string>>
file<_Word, _Access>::get_variable_names(const scope& _scope) const
{
    const char* c = [&]()
    {
        switch (_scope)
        {
        case scope::element: return "e";
        case scope::global:  return "g";
        default: return "n";
        }
    }();

    int num_vars;
    {
        const auto err = ex_get_var_param(
            _handle, c, &num_vars
        );
        if (err < 0) return { exodus_error(err) };
    }

    if (!num_vars) return { std::vector<std::string>() };
    std::vector<std::string> variable_names(num_vars);
    for (auto& v : variable_names) v.resize(MAX_STR_LENGTH + 1);

    const auto err = ex_get_var_names(
        _handle, c, num_vars, (char**)&variable_names[0]
    );
    if (err < 0) return { exodus_error(err) };

    // Possibly make variable names a char[][] and then construct the vector
    // using the std::string constructor on each char[]
    for (auto& v : variable_names) v.shrink_to_fit();
    return { std::move(variable_names) };
}
FWD_DEC_READ(unsigned long, result<std::vector<std::string>>, get_variable_names, const scope&);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<block<_Word>>>
file<_Word, _Access>::get_blocks() const
{
   const auto res = get_info();
   if (!res) return { res.error() } ;

    std::vector<int> ids(res.value().num_elem_blk);
    EXO_CHECK(ex_get_ids(_handle, EX_ELEM_BLOCK, ids.data()));

    std::vector<block<_Word>> blocks(ids.size());
    for (uint32_t i = 0; i < blocks.size(); i++)
    {
        ex_block block{0};
        char type[MAX_STR_LENGTH];
        EXO_CHECK(ex_get_block(
            _handle, 
            EX_ELEM_BLOCK,
            ids[i],
            type,
            &blocks[i].info.elements,
            &blocks[i].info.nodes_per_elem,
            &blocks[i].info.edges_per_entry,
            &blocks[i].info.faces_per_entry,
            &blocks[i].info.attributes
        ));

        blocks[i].info.id = ids[i];
        blocks[i].info.type = std::string(type);

        char name[MAX_STR_LENGTH];
        EXO_CHECK(ex_get_name(_handle, EX_ELEM_BLOCK, ids[i], name));
        if (strlen(name))
            blocks[i].info.name = std::string(name);
    }
    return { std::move(blocks) };
}
FWD_DEC_READ(unsigned long, result<std::vector<block<unsigned long>>>, get_blocks);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::get_block_data(
    const std::string& name, 
    integer<_Word> time_step, 
    block<_Word>& block) const
{
    const auto var_names_res = get_variable_names(scope::element);
    if (!var_names_res) return { var_names_res.error() };

    const auto& var_names = var_names_res.value();

    int index = -1;
    for (int i = 0; i < var_names.size(); i++)
        if (var_names[i] == name)
        {
            index = i;
            break;
        }
    
    if (index == -1) return { error_code::VarNotPresent };

    // If there is no data at all, construct the map
    if (!block.data.has_value()) block.data.emplace();
    auto& map = block.data.value();

    // If the map is missing the variable name construct the vector
    // otherwise reset the memory
    if (!map.count(name)) map.insert(std::pair(name, std::vector<_Word>()));
    else map.at(name).clear();

    auto& vec = map.at(name);
    const auto err = ex_get_var(_handle, time_step, EX_ELEM_BLOCK, index, block.info.id, block.info.elements, vec.data());
    if (err < 0) return { exodus_error(err) };
    return { };
}
FWD_DEC_READ(unsigned long, result<void>, get_block_data, const std::string&, int64_t, block<unsigned long>&);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<std::vector<_Word>>>
file<_Word, _Access>::get_element_variable_values(
    const integer<_Word> time_step, 
    const integer<_Word> var_ind) const
{
    const auto time_step_res = get_time_values();
    if (!time_step_res) return { time_step_res.error() };
    const auto& time_steps = time_step_res.value();

    if (time_step > time_steps.size()) return { error_code::TimeStepNotPresent };
    if (!time_step) return { error_code::TimeStepIndexOutOfBounds };
    if (!var_ind)   return { error_code::VariableIndexOutOfBounds };

    const auto res = get_info();
    if (!res) return { res.error() };

    const auto& info = res.value();

    std::vector<std::vector<_Word>> ret;
    if (!info.num_elem_blk) return { std::move(ret) };

    ret.resize(info.num_elem_blk);

    std::vector<int> ids(info.num_elem_blk);
    auto error = ex_get_elem_blk_ids(_handle, ids.data());
    if (error < 0) return { exodus_error(error) };

    for (uint32_t i = 0; i < info.num_elem_blk; i++)
    {
        char elem_type[MAX_STR_LENGTH];
        int num_elem_this_blk, num_nodes_per_elem, num_attr;

        error = ex_get_elem_block(_handle, ids[i], elem_type, &num_elem_this_blk, &num_nodes_per_elem, &num_attr);
        if (error < 0) return { exodus_error(error) };

        if (!num_elem_this_blk) continue;
        ret[i].resize(num_elem_this_blk);
        error = ex_get_elem_var(_handle, time_step, var_ind, ids[i], num_elem_this_blk, ret[i].data());
        if (error < 0) return { exodus_error(error) };
    }

    return { std::move(ret) };
}
FWD_DEC_READ(unsigned long, result<std::vector<std::vector<unsigned long>>>, get_element_variable_values, const int64_t, const int64_t);

#pragma endregion READ

}