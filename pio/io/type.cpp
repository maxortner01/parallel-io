#include "type.hh"

namespace pio::io
{
    const func_ptr<type<NC_DOUBLE>::integral_type> type<NC_DOUBLE>::func = &ncmpi_iget_vara_double;
    const func_ptr<type<NC_FLOAT>::integral_type> type<NC_FLOAT>::func = &ncmpi_iget_vara_float;
    const func_ptr<type<NC_CHAR>::integral_type> type<NC_CHAR>::func = &ncmpi_iget_vara_text;
    const func_ptr<type<NC_INT>::integral_type> type<NC_INT>::func = &ncmpi_iget_vara_int;
}