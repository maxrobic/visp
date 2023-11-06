#ifndef VISP_PYTHON_CORE_HPP
#define VISP_PYTHON_CORE_HPP
#include <sstream>
#include <cstring>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <visp3/core/vpArray2D.h>
#include <visp3/core/vpColVector.h>
#include <visp3/core/vpHomogeneousMatrix.h>
#include <visp3/core/vpImage.h>
#include <visp3/core/vpRGBf.h>



namespace py = pybind11;

template<typename Item>
using np_array_cf = py::array_t<Item, py::array::c_style | py::array::forcecast>;

/*
Create a buffer info for a row major array
*/
template<typename T, unsigned N>
py::buffer_info make_array_buffer(T *data, std::array<unsigned, N> dims, bool readonly)
{
  std::array<ssize_t, N> strides;
  for (unsigned i = 0; i < N; i++) {
    unsigned s = sizeof(T);
    for (unsigned j = i + 1; j < N; ++j) {
      s *= dims[j];
    }
    strides[i] = s;
  }
  return py::buffer_info(
      data,                             /* Pointer to data (nullptr -> ask NumPy to allocate!) */
      sizeof(T),                        /* Size of one item */
      py::format_descriptor<T>::value,  /* Buffer format */
      N,                                /* How many dimensions? */
      dims,                             /* Number of elements for each dimension */
      strides,                           /* Strides for each dimension */
      readonly
  );
}

std::string shape_to_string(const std::vector<ssize_t> &shape)
{
  std::stringstream ss;
  ss << "(";
  for (int i = 0; i < int(shape.size()) - 1; ++i) {
    ss << shape[i] << ",";
  }
  if (shape.size() > 0) {
    ss << shape[shape.size() - 1];
  }
  ss << ")";
  return ss.str();
}

template<typename Item>
void verify_array_shape_and_dims(np_array_cf<Item> np_array, unsigned dims, const char *class_name)
{
  py::buffer_info buffer = np_array.request();
  std::vector<ssize_t> shape = buffer.shape;
  if (shape.size() != dims) {
    std::stringstream ss;
    ss << "Tried to instanciate " << class_name
      << " that expects a " << dims << "D array but got a numpy array of shape "
      << shape_to_string(shape);

    throw std::runtime_error(ss.str());
  }
}
template<typename Item>
void verify_array_shape_and_dims(np_array_cf<Item> np_array, std::vector<ssize_t> expected_dims, const char *class_name)
{
  verify_array_shape_and_dims(np_array, expected_dims.size(), class_name);
  py::buffer_info buffer = np_array.request();
  std::vector<ssize_t> shape = buffer.shape;
  bool invalid_shape = false;
  for (unsigned int i = 0; i < expected_dims.size(); ++i) {
    if (shape[i] != expected_dims[i]) {
      invalid_shape = true;
      break;
    }
  }
  if (invalid_shape) {
    std::stringstream ss;
    ss << "Tried to instanciate " << class_name
      << " that expects an array of dimensions " << shape_to_string(expected_dims)
      << " but got a numpy array of shape " << shape_to_string(shape);

    throw std::runtime_error(ss.str());
  }
}
template<typename Item>
void copy_data_from_np(np_array_cf<Item> src, Item *dest)
{
  py::buffer_info buffer = src.request();
  std::vector<ssize_t> shape = buffer.shape;
  unsigned int elements = 1;
  for (ssize_t dim : shape) {
    elements *= dim;
  }
  const Item *data = (Item *)buffer.ptr;
  std::memcpy(dest, data, elements * sizeof(Item));

}

/*Array2D and its children*/

/*Get buffer infos : used in def_buffer and the .numpy() function*/
template<typename T> py::buffer_info get_buffer_info(T &) = delete;
template<typename T,
  template <typename> class Array,
  typename std::enable_if<std::is_same<vpArray2D<T>, Array<T>>::value, bool>::type = true>
py::buffer_info get_buffer_info(Array<T> &array)
{
  return make_array_buffer<T, 2>(array.data, { array.getRows(), array.getCols() }, false);
}

template<>
py::buffer_info get_buffer_info(vpMatrix &array)
{
  return make_array_buffer<double, 2>(array.data, { array.getRows(), array.getCols() }, false);
}

template<>
py::buffer_info get_buffer_info(vpColVector &array)
{
  return make_array_buffer<double, 1>(array.data, { array.getRows() }, false);
}
template<>
py::buffer_info get_buffer_info(vpRowVector &array)
{
  return make_array_buffer<double, 1>(array.data, { array.getCols() }, false);
}
template<>
py::buffer_info get_buffer_info(vpRotationMatrix &array)
{
  return make_array_buffer<double, 2>(array.data, { array.getRows(), array.getCols() }, true);
}
template<>
py::buffer_info get_buffer_info(vpHomogeneousMatrix &array)
{
  return make_array_buffer<double, 2>(array.data, { array.getRows(), array.getCols() }, true);
}

/*Array 2D indexing*/
template<typename PyClass, typename Class, typename Item>
void define_get_item_2d_array(PyClass &pyClass)
{

  pyClass.def("__getitem__", [](const Class &self, std::pair<int, int> pair) -> Item {
    int i = pair.first, j = pair.second;
    const unsigned int rows = self.getRows(), cols = self.getCols();
    if (abs(i) > rows || abs(j) > cols) {
      std::stringstream ss;
      ss << "Invalid indexing into a 2D array: got indices " << shape_to_string({ i, j })
        << " but array has dimensions " << shape_to_string({ rows, cols });
      throw std::runtime_error(ss.str());
    }
    if (i < 0) {
      i = rows + i;
    }
    if (j < 0) {
      j = cols + j;
    }
    return self[i][j];
  });
  pyClass.def("__getitem__", [](const Class &self, int i) -> np_array_cf<Item> {
    const unsigned int rows = self.getRows();
    if (abs(i) > rows) {
      std::stringstream ss;
      ss << "Invalid indexing into a 2D array: got row index " << shape_to_string({ i })
        << " but array has " << rows << " rows";
      throw std::runtime_error(ss.str());
    }
    if (i < 0) {
      i = rows + i;
    }
    return (py::cast(self).template cast<np_array_cf<Item> >())[py::cast(i)].template cast<np_array_cf<Item>>();
  });
  pyClass.def("__getitem__", [](const Class &self, py::slice slice) -> np_array_cf<Item> {
    return (py::cast(self).template cast<np_array_cf<Item> >())[slice].template cast<np_array_cf<Item>>();
  });
  pyClass.def("__getitem__", [](const Class &self, py::tuple tuple) {
    return (py::cast(self).template cast<np_array_cf<Item> >())[tuple].template cast<py::array_t<Item>>();
  });

}

const char *numpy_fn_doc_writable = R"doc(
  Numpy view of the underlying array data.
  This numpy view can be used to directly modify the array.
)doc";

const char *numpy_fn_doc_nonwritable = R"doc(
  Numpy view of the underlying array data.
  This numpy view cannot be modified.
  If you try to modify the array, an exception will be raised.
)doc";


template<typename T>
void bindings_vpArray2D(py::class_<vpArray2D<T>> &pyArray2D)
{

  pyArray2D.def_buffer(&get_buffer_info<T, vpArray2D>);

  pyArray2D.def("numpy", [](vpArray2D<T> &self) -> np_array_cf<T> {
    return py::cast(self).template cast<np_array_cf<T> >();
  }, numpy_fn_doc_writable);

  pyArray2D.def(py::init([](np_array_cf<T> &np_array) {
    verify_array_shape_and_dims(np_array, 2, "ViSP 2D array");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpArray2D<T> result(shape[0], shape[1]);
    copy_data_from_np(np_array, result.data);
    return result;
  }), R"doc(
Construct a 2D ViSP array by **copying** a 2D numpy array.

:param np_array: The numpy array to copy.

)doc", py::arg("np_array"));

  define_get_item_2d_array<py::class_<vpArray2D<T>>, vpArray2D<T>, T>(pyArray2D);
}

void bindings_vpMatrix(py::class_<vpMatrix, vpArray2D<double>> &pyMatrix)
{

  pyMatrix.def_buffer(&get_buffer_info<vpMatrix>);

  pyMatrix.def("numpy", [](vpMatrix &self) -> np_array_cf<double> {
    return py::cast(self).cast<np_array_cf<double>>();
  }, numpy_fn_doc_writable);

  pyMatrix.def(py::init([](np_array_cf<double> np_array) {
    verify_array_shape_and_dims(np_array, 2, "ViSP Matrix");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpMatrix result(shape[0], shape[1]);
    copy_data_from_np(np_array, result.data);
    return result;
  }), R"doc(
Construct a matrix by **copying** a 2D numpy array.

:param np_array: The numpy array to copy.

)doc", py::arg("np_array"));

  define_get_item_2d_array<py::class_<vpMatrix, vpArray2D<double>>, vpMatrix, double>(pyMatrix);
}


void bindings_vpColVector(py::class_<vpColVector, vpArray2D<double>> &pyColVector)
{
  pyColVector.def_buffer(&get_buffer_info<vpColVector>);

  pyColVector.def("numpy", [](vpColVector &self) -> np_array_cf<double> {
    return py::cast(self).cast<np_array_cf<double>>();
  }, numpy_fn_doc_writable);

  pyColVector.def(py::init([](np_array_cf<double> np_array) {
    verify_array_shape_and_dims(np_array, 1, "ViSP column vector");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpColVector result(shape[0]);
    copy_data_from_np(np_array, result.data);
    return result;
  }), R"doc(
Construct a column vector by **copying** a 1D numpy array.

:param np_array: The numpy 1D array to copy.

)doc", py::arg("np_array"));

}

void bindings_vpRowVector(py::class_<vpRowVector, vpArray2D<double>> &pyRowVector)
{
  pyRowVector.def_buffer(&get_buffer_info<vpRowVector>);
  pyRowVector.def("numpy", [](vpRowVector &self) -> np_array_cf<double> {
    return np_array_cf<double>(get_buffer_info<vpRowVector>(self), py::cast(self));
  }, numpy_fn_doc_writable);
  pyRowVector.def(py::init([](np_array_cf<double> np_array) {
    verify_array_shape_and_dims(np_array, 1, "ViSP row vector");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpRowVector result(shape[0]);
    copy_data_from_np(np_array, result.data);
    return result;
  }), R"doc(
Construct a row vector by **copying** a 1D numpy array.

:param np_array: The numpy 1D array to copy.

)doc", py::arg("np_array"));
}


void bindings_vpRotationMatrix(py::class_<vpRotationMatrix, vpArray2D<double>> &pyRotationMatrix)
{

  pyRotationMatrix.def_buffer(&get_buffer_info<vpRotationMatrix>);
  pyRotationMatrix.def("numpy", [](vpRotationMatrix &self) -> np_array_cf<double> {
    return py::cast(self).cast<np_array_cf<double>>();
  }, numpy_fn_doc_nonwritable);
  pyRotationMatrix.def(py::init([](np_array_cf<double> np_array) {
    verify_array_shape_and_dims(np_array, { 3, 3 }, "ViSP rotation matrix");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpRotationMatrix result;
    copy_data_from_np(np_array, result.data);
    if (!result.isARotationMatrix()) {
      throw std::runtime_error("Input numpy array is not a valid rotation matrix");
    }
    return result;
  }), R"doc(
Construct a rotation matrix by **copying** a 2D numpy array.
This numpy array should be of dimensions :math:`3 \times 3` and be a valid rotation matrix.
If it is not a rotation matrix, an exception will be raised.

:param np_array: The numpy 1D array to copy.

)doc", py::arg("np_array"));
  define_get_item_2d_array<py::class_<vpRotationMatrix, vpArray2D<double>>, vpRotationMatrix, double>(pyRotationMatrix);
}

void bindings_vpHomogeneousMatrix(py::class_<vpHomogeneousMatrix, vpArray2D<double>> &pyHomogeneousMatrix)
{

  pyHomogeneousMatrix.def_buffer(get_buffer_info<vpHomogeneousMatrix>);
  pyHomogeneousMatrix.def("numpy", [](vpHomogeneousMatrix &self) -> np_array_cf<double> {
    return py::cast(self).cast<np_array_cf<double>>();
  }, numpy_fn_doc_nonwritable);

  pyHomogeneousMatrix.def(py::init([](np_array_cf<double> np_array) {
    verify_array_shape_and_dims(np_array, { 4, 4 }, "ViSP homogeneous matrix");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpHomogeneousMatrix result;
    copy_data_from_np(np_array, result.data);
    if (!result.isAnHomogeneousMatrix()) {
      throw std::runtime_error("Input numpy array is not a valid homogeneous matrix");
    }
    return result;
  }), R"doc(
Construct a homogeneous matrix by **copying** a 2D numpy array.
This numpy array should be of dimensions :math:`4 \times 4` and be a valid homogeneous matrix.
If it is not a homogeneous matrix, an exception will be raised.

:param np_array: The numpy 1D array to copy.

)doc", py::arg("np_array"));
  define_get_item_2d_array<py::class_<vpHomogeneousMatrix, vpArray2D<double>>, vpHomogeneousMatrix, double>(pyHomogeneousMatrix);
}


/*
  vpImage
*/
template<typename T>
typename std::enable_if<std::is_fundamental<T>::value, void>::type
bindings_vpImage(py::class_<vpImage<T>> &pyImage)
{
  pyImage.def_buffer([](vpImage<T> &image) -> py::buffer_info {
    return make_array_buffer<T, 2>(image.bitmap, { image.getHeight(), image.getWidth() }, false);
  });
  pyImage.def("numpy", [](vpImage<T> &self) -> np_array_cf<T> {
    return py::cast(self).template cast<np_array_cf<T>>();
  }, numpy_fn_doc_writable);

  pyImage.def(py::init([](np_array_cf<T> np_array) {
    verify_array_shape_and_dims(np_array, 2, "ViSP Image");
    const std::vector<ssize_t> shape = np_array.request().shape;
    vpImage<T> result(shape[0], shape[1]);
    copy_data_from_np(np_array, result.bitmap);
    return result;
  }), R"doc(
Construct an image by **copying** a 2D numpy array.

:param np_array: The numpy array to copy.

)doc", py::arg("np_array"));
}

template<typename T>
typename std::enable_if<std::is_same<vpRGBa, T>::value, void>::type
bindings_vpImage(py::class_<vpImage<T>> &pyImage)
{
  static_assert(sizeof(T) == 4 * sizeof(unsigned char));
  pyImage.def_buffer([](vpImage<T> &image) -> py::buffer_info {
    return make_array_buffer<unsigned char, 3>(reinterpret_cast<unsigned char *>(image.bitmap), { image.getHeight(), image.getWidth(), 4 }, false);
  });
  pyImage.def("numpy", [](vpImage<T> &self) -> np_array_cf<unsigned char> {
    return py::cast(self).template cast<np_array_cf<unsigned char>>();
  }, numpy_fn_doc_writable);

  pyImage.def(py::init([](np_array_cf<unsigned char> np_array) {
    verify_array_shape_and_dims(np_array, 3, "ViSP RGBa image");
    const std::vector<ssize_t> shape = np_array.request().shape;
    if (shape[2] != 4) {
      throw std::runtime_error("Tried to copy a 3D numpy array that does not have 4 elements per pixel into a ViSP RGBA image");
    }
    vpImage<T> result(shape[0], shape[1]);
    copy_data_from_np(np_array, (unsigned char *)result.bitmap);
    return result;
  }), R"doc(
Construct an image by **copying** a 3D numpy array. this numpy array should be of the form :math:`H \times W \times 4`
where the 4 denotes the red, green, blue and alpha components of the image.

:param np_array: The numpy array to copy.

)doc", py::arg("np_array"));

}
template<typename T>
typename std::enable_if<std::is_same<vpRGBf, T>::value, void>::type
bindings_vpImage(py::class_<vpImage<T>> &pyImage)
{
  static_assert(sizeof(T) == 3 * sizeof(float));
  pyImage.def_buffer([](vpImage<T> &image) -> py::buffer_info {
    return make_array_buffer<float, 3>(reinterpret_cast<float *>(image.bitmap), { image.getHeight(), image.getWidth(), 3 }, false);
  });
}




#endif
