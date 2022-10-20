#include <pybind11/pybind11.h>
#include <pybind11/eigen.h>
#include <pybind11/stl.h>

#include <FieldGenerator.H>

namespace py = pybind11;

#include "TensorToArray.H"

void FieldGeneratorClasses(py::module &m) {

  m.doc() = "FieldGenerator class bindings\n\n"
    "FieldGenerator\n"
    "==============\n"
    "This class computes surfaces and volumes for visualizing the physical\n"
    "quantities implied by your basis and associated coefficients.\n\n"
    "The generator is constructed by passing a vector of desired times\n"
    "that must be in the coefficient object and list of lower bounds,\n"
    "a list of upper bounds, and a list of knots per dimension.  These\n"
    "lists all have rank 3 for (x, y, z).  For a two-dimensional surface,\n"
    "one of the knot array values must be zero.  The member functions\n"
    "slices and volumes, called with the basis and coefficient objects,\n"
    "return a numpy.ndarray containing the field evaluations.  Each of\n"
    "these functions returns a dictionary of times to a dictionary of\n"
    "field names to numpy.ndarrays at each time.  There are also members\n"
    "which will write these generated fields to files. See help(pyEXP.basis)\n"
    "and help(pyEXP.coefs) for info on the basis and coefficient objects.\n\n";

  using namespace Field;

  py::class_<Field::FieldGenerator, std::shared_ptr<Field::FieldGenerator>>
    f(m, "FieldGenerator");

  f.def(py::init<const std::vector<double>, const std::vector<double>,
	const std::vector<double>, const std::vector<int>>(),
	"Create fields for given times and lower and upper bounds and "
	"grid sizes", py::arg("times"), py::arg("lower"), py::arg("upper"),
	py::arg("gridsize"));

  f.def("slices", &Field::FieldGenerator::slices,
	"Return a dictionary of grids (2d numpy arrays) indexed by "
	"time and field type", py::arg("basis"), py::arg("coefs"));
  
  f.def("histo", &Field::FieldGenerator::histogram,
	"Return a density histogram (2d numpy arrays)",
	py::arg("reader"),
	py::arg("center") = std::vector<double>(3, 0.0));

  f.def("file_slices", &Field::FieldGenerator::file_slices,
	"Write 2d field grids to files using the supplied string prefix",
	py::arg("basis"), py::arg("coefs"), py::arg("filename"),
	py::arg("dir")="");

  f.def("volumes", [](FieldGenerator& A,
		      Basis::BasisPtr basis, Coefs::CoefsPtr coefs)
  {
    std::map<double, std::map<std::string, py::array_t<float>>> ret;
    auto vols = A.volumes(basis, coefs);
    for (auto & v : vols) {
      for (auto & u : v.second) {
	ret[v.first][u.first] = make_ndarray<float>(u.second);
      }
    }

    return ret;
  },
    "Returns a dictionary of volume grids (3d numpy arrays) indexed by "
    "time and field type");

  f.def("file_volumes", &Field::FieldGenerator::file_volumes,
	"Write 3d field grids to files using the supplied string prefix");
}
