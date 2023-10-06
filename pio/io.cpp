#include "io.hh"

namespace pio::io
{
    const func_ptr<Type<NC_DOUBLE>::type> Type<NC_DOUBLE>::func = &ncmpi_iget_vara_double;
    const func_ptr<Type<NC_FLOAT>::type> Type<NC_FLOAT>::func = &ncmpi_iget_vara_float;
    const func_ptr<Type<NC_CHAR>::type> Type<NC_CHAR>::func = &ncmpi_iget_vara_text;
    const func_ptr<Type<NC_INT>::type> Type<NC_INT>::func = &ncmpi_iget_vara_int;
}