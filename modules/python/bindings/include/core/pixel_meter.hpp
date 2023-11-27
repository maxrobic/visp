/*
 * ViSP, open source Visual Servoing Platform software.
 * Copyright (C) 2005 - 2023 by Inria. All rights reserved.
 *
 * This software is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * See the file LICENSE.txt at the root directory of this source
 * distribution for additional information about the GNU GPL.
 *
 * For using ViSP with software that can not be combined with the GNU
 * GPL, please contact Inria about acquiring a ViSP Professional
 * Edition License.
 *
 * See https://visp.inria.fr for more information.
 *
 * This software was developed at:
 * Inria Rennes - Bretagne Atlantique
 * Campus Universitaire de Beaulieu
 * 35042 Rennes Cedex
 * France
 *
 * If you have questions regarding the use of this file, please contact
 * Inria at visp@inria.fr
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Description:
 * Python bindings.
 */

#ifndef VISP_PYTHON_CORE_PIXEL_METER_HPP
#define VISP_PYTHON_CORE_PIXEL_METER_HPP

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <visp3/core/vpPixelMeterConversion.h>
#include <visp3/core/vpMeterPixelConversion.h>

#include "core/utils.hpp"

void bindings_vpPixelMeterConversion(py::class_<vpPixelMeterConversion> &pyPM)
{
  pyPM.def_static("convertPoints", [](const vpCameraParameters &cam, const py::array_t<double> &us, const py::array_t<double> &vs) {
    py::buffer_info bufu = us.request(), bufv = vs.request();
    if (bufu.ndim != bufv.ndim || bufu.shape != bufv.shape) {
      std::stringstream ss;
      ss << "us and vs must have the same number of dimensions and same number of elements, but got us = " << shape_to_string(bufu.shape);
      ss << "and vs = " << shape_to_string(bufv.shape);
      throw std::runtime_error(ss.str());
    }
    py::array_t<double> xs(bufu.shape);
    py::array_t<double> ys(bufv.shape);

    const double *u_ptr = static_cast<const double *>(bufu.ptr);
    const double *v_ptr = static_cast<const double *>(bufv.ptr);
    double *x_ptr = static_cast<double *>(xs.request().ptr);
    double *y_ptr = static_cast<double *>(ys.request().ptr);

    for (ssize_t i = 0; i < bufu.size; ++i) {
      vpPixelMeterConversion::convertPoint(cam, u_ptr[i], v_ptr[i], x_ptr[i], y_ptr[i]);
    }

    return std::make_tuple(std::move(xs), std::move(ys));

  }, R"doc(
Convert a set of 2D pixel coordinates to normalized coordinates.
:param cam: The camera intrinsics with which to convert pixels to normalized coordinates.
:param us: The pixel coordinates along the horizontal axis.
:param vs: The pixel coordinates along the vertical axis.

:raises RuntimeError: If us and vs do not have the same dimensions and shape.

:return: A tuple containing the x and y normalized coordinates of the input pixels.

)doc", py::arg("cam"), py::arg("us"), py::arg("vs"));
}

void bindings_vpMeterPixelConversion(py::class_<vpMeterPixelConversion> &pyMP)
{
  pyMP.def_static("convertPoints", [](const vpCameraParameters &cam, const py::array_t<double> &xs, const py::array_t<double> &ys) {
    py::buffer_info bufx = xs.request(), bufy = ys.request();
    if (bufx.ndim != bufy.ndim || bufx.shape != bufy.shape) {
      std::stringstream ss;
      ss << "xs and ys must have the same number of dimensions and same number of elements, but got xs = " << shape_to_string(bufx.shape);
      ss << "and ys = " << shape_to_string(bufy.shape);
      throw std::runtime_error(ss.str());
    }
    py::array_t<double> us(bufx.shape);
    py::array_t<double> vs(bufy.shape);

    const double *x_ptr = static_cast<const double *>(bufx.ptr);
    const double *y_ptr = static_cast<const double *>(bufy.ptr);
    double *u_ptr = static_cast<double *>(us.request().ptr);
    double *v_ptr = static_cast<double *>(vs.request().ptr);

    for (ssize_t i = 0; i < bufx.size; ++i) {
      vpMeterPixelConversion::convertPoint(cam, x_ptr[i], y_ptr[i], u_ptr[i], v_ptr[i]);
    }

    return std::make_tuple(std::move(us), std::move(vs));

  }, R"doc(
Convert a set of 2D normalized coordinates to pixel coordinates.
:param cam: The camera intrinsics with which to convert normalized coordinates to pixels.
:param xs: The normalized coordinates along the horizontal axis.
:param ys: The normalized coordinates along the vertical axis.

:raises RuntimeError: If xs and ys do not have the same dimensions and shape.

:return: A tuple containing the u,v pixel coordinates of the input normalized coordinates.

)doc", py::arg("cam"), py::arg("xs"), py::arg("ys"));
}

#endif
