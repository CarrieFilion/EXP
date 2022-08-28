#include <pybind11/pybind11.h>
#include <pybind11/eigen.h>
#include <pybind11/stl.h>

#include <CoefFactory.H>

namespace py = pybind11;

void CoefFactoryClasses(py::module &m) {

  m.doc() = "CoefFactory class bindings";

  using namespace Coefs;

  class PyCoefStruct : public CoefStruct
  {
  public:
    
    // Inherit the constructors
    using CoefStruct::CoefStruct;

    bool read(std::istream& in, bool exp_type) override {
      PYBIND11_OVERRIDE_PURE(bool, CoefStruct, read, in, exp_type);
    }
  };

  class PyCoefs : public Coefs
  {
  protected:
    void readNativeCoefs(const std::string& file) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, readNativeCoefs, file);
    }
    
    std::string getYAML() override {
      PYBIND11_OVERRIDE_PURE(std::string, Coefs, getYaml,);
    }
    
    void WriteH5Params(HighFive::File& file) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, WriteH5Params, file);
    }

    unsigned WriteH5Times(HighFive::Group& group, unsigned count) override {
      PYBIND11_OVERRIDE_PURE(unsigned, Coefs, WriteH5Times, group, count);
    }

  public:
    // Inherit the constructors
    using Coefs::Coefs;

    Eigen::MatrixXcd& operator()(double time) override {
      PYBIND11_OVERRIDE_PURE(Eigen::MatrixXcd&, Coefs, operator(), time);
    }

    std::shared_ptr<CoefStruct> getCoefStruct(double time) override {
      PYBIND11_OVERRIDE_PURE(std::shared_ptr<CoefStruct>, Coefs, getCoefStruct,
			     time);
    }

    void dump(int mmin, int mmax, int nmin, int nmax) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, dump,
			     mmin, mmax, nmin, nmax);
    }

    std::vector<double> Times() override {
      PYBIND11_OVERRIDE_PURE(std::vector<double>, Coefs, Times,);
    }

    void WriteH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, WriteH5Coefs, prefix);
    }

    void ExtendH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, ExtendH5Coefs, prefix);
    }

    Eigen::MatrixXd& Power() override {
      PYBIND11_OVERRIDE_PURE(Eigen::MatrixXd&, Coefs, Power,);
    }

    bool CompareStanzas(std::shared_ptr<Coefs> check) override {
      PYBIND11_OVERRIDE_PURE(bool, Coefs, CompareStanzas, check);
    }

    void add(CoefStrPtr coef) override {
      PYBIND11_OVERRIDE_PURE(void, Coefs, add, coef);
    }

  };

  class PySphCoefs : public SphCoefs
  {
  protected:
    void readNativeCoefs(const std::string& file) override {
      PYBIND11_OVERRIDE(void, SphCoefs, readNativeCoefs, file);
    }
    
    std::string getYAML() override {
      PYBIND11_OVERRIDE(std::string, SphCoefs, getYAML,);
    }
    
    void WriteH5Params(HighFive::File& file) override {
      PYBIND11_OVERRIDE(void, SphCoefs, WriteH5Params, file);
    }

    unsigned WriteH5Times(HighFive::Group& group, unsigned count) override {
      PYBIND11_OVERRIDE(unsigned, SphCoefs, WriteH5Times, group, count);
    }

  public:
    // Inherit the constructors
    using SphCoefs::SphCoefs;

    Eigen::MatrixXcd& operator()(double time) override {
      PYBIND11_OVERRIDE(Eigen::MatrixXcd&, SphCoefs, operator(), time);
    }

    std::shared_ptr<CoefStruct> getCoefStruct(double time) override {
      PYBIND11_OVERRIDE(std::shared_ptr<CoefStruct>, SphCoefs, getCoefStruct,
			time);
    }

    void dump(int mmin, int mmax, int nmin, int nmax) override {
      PYBIND11_OVERRIDE(void, SphCoefs,dump,
			mmin, mmax, nmin, nmax);
    }

    std::vector<double> Times() override {
      PYBIND11_OVERRIDE(std::vector<double>, SphCoefs, Times,);
    }

    void WriteH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE(void, SphCoefs, WriteH5Coefs, prefix);
    }

    void ExtendH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE(void, SphCoefs, ExtendH5Coefs, prefix);
    }

    Eigen::MatrixXd& Power() override {
      PYBIND11_OVERRIDE(Eigen::MatrixXd&, SphCoefs, Power,);
    }

    bool CompareStanzas(std::shared_ptr<Coefs> check) override {
      PYBIND11_OVERRIDE(bool, SphCoefs, CompareStanzas, check);
    }

    void add(CoefStrPtr coef) override {
      PYBIND11_OVERRIDE(void, SphCoefs,	add, coef);
    }

  };

  class PyCylCoefs : public CylCoefs
  {
  protected:
    void readNativeCoefs(const std::string& file) override {
      PYBIND11_OVERRIDE(void, CylCoefs,	readNativeCoefs, file);
    }
    
    std::string getYAML() override {
      PYBIND11_OVERRIDE(std::string, CylCoefs, getYAML,);
    }
    
    void WriteH5Params(HighFive::File& file) override {
      PYBIND11_OVERRIDE(void, CylCoefs, WriteH5Params, file);
    }

    unsigned WriteH5Times(HighFive::Group& group, unsigned count) override {
      PYBIND11_OVERRIDE(unsigned, CylCoefs, WriteH5Times, group, count);
    }

  public:
    // Inherit the constructors
    using CylCoefs::CylCoefs;

    Eigen::MatrixXcd& operator()(double time) override {
      PYBIND11_OVERRIDE(Eigen::MatrixXcd&, CylCoefs, operator(), time);
    }

    std::shared_ptr<CoefStruct> getCoefStruct(double time) override {
      PYBIND11_OVERRIDE(std::shared_ptr<CoefStruct>, CylCoefs, getCoefStruct,
			time);
    }

    void dump(int mmin, int mmax, int nmin, int nmax) override {
      PYBIND11_OVERRIDE(void, CylCoefs, dump,
			mmin, mmax, nmin, nmax);
    }

    std::vector<double> Times() override {
      PYBIND11_OVERRIDE(std::vector<double>, CylCoefs, Times,);
    }

    void WriteH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE(void, CylCoefs, WriteH5Coefs, prefix);
    }

    void ExtendH5Coefs(const std::string& prefix) override {
      PYBIND11_OVERRIDE(void, CylCoefs, ExtendH5Coefs, prefix);
    }

    Eigen::MatrixXd& Power() override {
      PYBIND11_OVERRIDE(Eigen::MatrixXd&, CylCoefs, Power,);
    }

    bool CompareStanzas(std::shared_ptr<Coefs> check) override {
      PYBIND11_OVERRIDE(bool, CylCoefs, CompareStanzas,	check);
    }

    void add(CoefStrPtr coef) override {
      PYBIND11_OVERRIDE(void, CylCoefs,	add, coef);
    }

  };

  py::class_<Coefs::CoefStruct, std::shared_ptr<Coefs::CoefStruct>, PyCoefStruct>(m, "CoefStruct")
    .def(py::init<>(), "Base class coefficient data structure object");

  py::class_<Coefs::SphStruct, std::shared_ptr<Coefs::SphStruct>, CoefStruct>(m, "SphStruct")
    .def(py::init<>(), "Spherical coefficient data structure object");

  py::class_<Coefs::CylStruct, std::shared_ptr<Coefs::CylStruct>, CoefStruct>(m, "CylStruct")
    .def(py::init<>(), "Cylindrical coefficient data structure object");

  py::class_<Coefs::Coefs, std::shared_ptr<Coefs::Coefs>, PyCoefs>(m, "Coefs")
    .def(py::init<std::string, bool>(), "Base coefficient container class")
    .def("operator()",     &Coefs::Coefs::operator(),
	 "Return the coefficient matrix for the desired time")
    .def("add",            &Coefs::Coefs::add,
	 "Add a coefficient structure to the coefficient container")
    .def("getCoefStruct",  &Coefs::Coefs::getCoefStruct,
	 "Return the coefficient structure for the desired time")
    .def("Times",          &Coefs::Coefs::Times,
	 "Return a list of times for coefficient sets current in the container")
    .def("WriteH5Coefs",   &Coefs::Coefs::WriteH5Coefs,
	 "Write the coefficients into an EXP HDF5 coefficieint file")
    .def("ExtendH5Coefs",  &Coefs::Coefs::ExtendH5Coefs,
	 "Extend an existing EXP HDF5 coefficient file with the coefficient in the container")
    .def("Power",          &Coefs::Coefs::Power,
	 "Return a ndarray table of the full power for the top-level harmonic index as function of time")
    .def_static("makecoefs", &Coefs::Coefs::makecoefs,
		"Create a new coefficient instance compatible with the supplied coefficient structure");

  py::class_<Coefs::SphCoefs, std::shared_ptr<Coefs::SphCoefs>, PySphCoefs, Coefs::Coefs>(m, "SphCoefs", "Container for spherical coefficients")
    .def(py::init<bool>());

  py::class_<Coefs::CylCoefs, std::shared_ptr<Coefs::CylCoefs>, PyCylCoefs, Coefs::Coefs>(m, "CylCoefs", "Container for cylindrical coefficients")
    .def(py::init<bool>());
}

