#include <exodusII.h>
#include <iostream>
#include <mpi.h>
#include <optional>
#include <pnetcdf.h>

template <typename T> class result {
  int err;
  std::optional<T> _value;

public:
  result(int e) : _value(std::nullopt), err(e) {}

  result(const T &val) : _value(val), err(NC_NOERR) {}

  auto good() const { return _value.has_value(); }
  auto error() const { return err; }

  auto &value() const { return _value.value(); }
};

namespace pncdf {
struct file {
  struct info {
    int dimensions, variables, attributes;
  };

  file(const std::string &file_location) {
    err = ncmpi_open(MPI_COMM_WORLD, file_location.c_str(), NC_NOWRITE,
                     MPI_INFO_NULL, &handle);

    if (err != NC_NOERR)
      _good = false;
    else
      _good = true;
  }

  file(const file &) = delete;

  ~file() { close(); }

  void close() {
    if (good())
      err = ncmpi_close(handle);
  }

  result<info> inquire() const {
    info i;
    int unlimited;
    int error = ncmpi_inq(handle, &i.dimensions, &i.variables, &i.attributes,
                          &unlimited);

    if (error != NC_NOERR)
      return result<info>(error);

    return result<info>(i);
  }

  auto error_string() const { return std::string(ncmpi_strerror(err)); }
  auto error() const { return err; }
  bool good() const { return _good; }

  operator bool() const { return good(); }

private:
  int handle, err;
  bool _good;
};
} // namespace pncdf

namespace exodus {
template <typename _Word,
          typename = std::enable_if_t<std::is_same<_Word, float>::value ||
                                      std::is_same<_Word, double>::value>>
struct file {
  struct info {
    std::string title;
    int num_dim, num_nodes, num_elem, num_elem_blk, num_node_sets,
        num_side_sets;
  };

  file(const std::string &filename) {
    int comp_ws = sizeof(_Word);
    int io_ws = 0;
    float version;
    handle = ex_open(filename.c_str(), EX_READ, &comp_ws, &io_ws, &version);

    _good = (handle < 0 ? false : true);
  }

  file(const file &) = delete;

  ~file() { close(); }

  void close() {
    if (_good)
      ex_close(handle);
  }

  result<info> get_info() const {
    info i;
    char title[MAX_LINE_LENGTH];
    const auto ret =
        ex_get_init(handle, title, &i.num_dim, &i.num_nodes, &i.num_elem,
                    &i.num_elem_blk, &i.num_node_sets, &i.num_side_sets);

    if (ret < 0)
      return result<info>(ret);

    i.title = std::string(title);
    return result(i);
  }

  auto good() const { return _good; }
  operator bool() const { return good(); }

private:
  int handle, err;
  bool _good;
};
} // namespace exodus

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  char processor_name[MPI_MAX_PROCESSOR_NAME];
  int name_len;
  MPI_Get_processor_name(processor_name, &name_len);

  printf("Hello from processor %s, rank %d out of %d processors.\n",
         processor_name, world_rank, world_size);

  exodus::file<float> file("../box-hex.exo");
  if (!file) {
    std::cout << "Error opening file\n";
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 2;
  }

  const auto res = file.get_info();

  if (res.good()) {
    std::cout << "Title: " << res.value().title << "\n";
    std::cout << "Dimensions: " << res.value().num_dim << "\n";
    std::cout << "Nodes: " << res.value().num_nodes << "\n";
    std::cout << "Num Elems: " << res.value().num_elem << "\n";
    std::cout << "Num Elem Blk: " << res.value().num_elem_blk << "\n";
    std::cout << "Num Node Sets: " << res.value().num_node_sets << "\n";
    std::cout << "Num Side Sets: " << res.value().num_side_sets << "\n";
  }

  /*
  double* x = (double*)calloc(num_nodes, sizeof(double));
  double* y = (double*)calloc(num_nodes, sizeof(double));
  double* z = (double*)calloc(num_nodes, sizeof(double));

  const auto error = ex_get_coord(exoid, x, y, z);
  if (error < 0)
  {
      std::cout << "Error getting coordinates\n";
  }
  else
  {
      for (uint32_t i = 0; i < num_nodes; i++)
          std::cout << x[i] << " " << y[i] << " " << z[i] << "\n";
  }

  std::free(x);
  std::free(y);
  std::free(z);
  const auto ret = ex_close(exoid);
  if (ret)
  {
      std::cout << "Closed with warning or error\n";
  }*/

  /*
  pncdf::file exo("../box-hex.exo");
  if (!exo)
  {
      std::cout << "Error opening file: " << exo.error_string() << "\n";
      MPI_Abort(MPI_COMM_WORLD, 1);
      return 1;
  }

  const auto inq = exo.inquire();

  std::cout << "File info:\nndims: " << inq.value().dimensions << "\nnvars: " <<
  inq.value().variables << "\nngatt: " << inq.value().attributes << "\n";

  exo.close();*/

  MPI_Finalize();
}