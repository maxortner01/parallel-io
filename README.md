# parallel-io
Meant to serve as a sandbox for testing parallel I/O file reading/writing for possible use as external driver library for [FleCSI](https://github.com/flecsi/flecsi) I/O.

Dependencies are not built in at the moment, but `spack add exodusii parallel-netcdf mpi` should do it.

## Basic Usage
The currently supported file types are [Exodus II](https://www.osti.gov/servlets/purl/10102115) and [NetCDF](https://www.unidata.ucar.edu/software/netcdf/). The relevant objects are found in `pio::exodus` and `pio::netcdf` respectively. 

### Requests
The framework makes async requests and uses MPI to do so (utilizing [PNetCDF](https://parallel-netcdf.github.io/)). Thus, some retrieval or write methods will return a `io::Promise<...>` object that represts a set of requests each with a corresponding type. For example, if you want two variables "name" and "amount" that are `type::Char` and `type::Double` respectively, you can make a request for the data with 
```C++
const auto promise = 
    file.get_variable_values<type::Char, type::Double>(
        "name", "amount"
    );

promise.wait_for_completion();
```
Then, we can retreive the data from the promise, but only after `wait_for_completion()` has been called. This can be done with 
```C++
// This gives std::vector<std::string>
const auto names   = promise.get<0>().get_strings();
// This gives a shared pointer to the data
const auto amounts = promise.get<1>().data; 
```