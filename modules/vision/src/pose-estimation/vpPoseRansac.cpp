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
 * Pose computation.
 */

/*!
  \file vpPoseRansac.cpp
  \brief function used to estimate a pose using the Ransac algorithm
*/

#include <algorithm> // std::count
#include <cmath>     // std::fabs
#include <float.h>   // DBL_MAX
#include <iostream>
#include <limits> // numeric_limits
#include <map>

#include <visp3/core/vpColVector.h>
#include <visp3/core/vpMath.h>
#include <visp3/core/vpRansac.h>
#include <visp3/vision/vpPose.h>
#include <visp3/vision/vpPoseException.h>

#include <thread>

#define eps 1e-6

namespace
{
// For std::map<vpPoint>
struct CompareObjectPointDegenerate
{
  bool operator()(const vpPoint &point1, const vpPoint &point2) const
  {
    const double dist1 =
      point1.get_oX() * point1.get_oX() + point1.get_oY() * point1.get_oY() + point1.get_oZ() * point1.get_oZ();
    const double dist2 =
      point2.get_oX() * point2.get_oX() + point2.get_oY() * point2.get_oY() + point2.get_oZ() * point2.get_oZ();
    if (dist1 - dist2 < -3 * eps * eps)
      return true;
    if (dist1 - dist2 > 3 * eps * eps)
      return false;

    if (point1.oP[0] - point2.oP[0] < -eps)
      return true;
    if (point1.oP[0] - point2.oP[0] > eps)
      return false;

    if (point1.oP[1] - point2.oP[1] < -eps)
      return true;
    if (point1.oP[1] - point2.oP[1] > eps)
      return false;

    if (point1.oP[2] - point2.oP[2] < -eps)
      return true;
    if (point1.oP[2] - point2.oP[2] > eps)
      return false;

    return false;
  }
};

// For std::map<vpPoint>
struct CompareImagePointDegenerate
{
  bool operator()(const vpPoint &point1, const vpPoint &point2) const
  {
    const double dist1 = point1.get_x() * point1.get_x() + point1.get_y() * point1.get_y();
    const double dist2 = point2.get_x() * point2.get_x() + point2.get_y() * point2.get_y();
    if (dist1 - dist2 < -2 * eps * eps)
      return true;
    if (dist1 - dist2 > 2 * eps * eps)
      return false;

    if (point1.p[0] - point2.p[0] < -eps)
      return true;
    if (point1.p[0] - point2.p[0] > eps)
      return false;

    if (point1.p[1] - point2.p[1] < -eps)
      return true;
    if (point1.p[1] - point2.p[1] > eps)
      return false;

    return false;
  }
};

// std::find_if
struct FindDegeneratePoint
{
  explicit FindDegeneratePoint(const vpPoint &pt) : m_pt(pt) { }

  bool operator()(const vpPoint &pt)
  {
    return ((std::fabs(m_pt.oP[0] - pt.oP[0]) < eps && std::fabs(m_pt.oP[1] - pt.oP[1]) < eps &&
             std::fabs(m_pt.oP[2] - pt.oP[2]) < eps) ||
            (std::fabs(m_pt.p[0] - pt.p[0]) < eps && std::fabs(m_pt.p[1] - pt.p[1]) < eps));
  }

  vpPoint m_pt;
};
} // namespace

bool vpPose::vpRansacFunctor::poseRansacImpl()
{
  const unsigned int size = (unsigned int)m_listOfUniquePoints.size();
  unsigned int nbMinRandom = 4;
  int nbTrials = 0;

  vpPoint p; // Point used to project using the estimated pose

  bool foundSolution = false;
  while (nbTrials < m_ransacMaxTrials && m_nbInliers < m_ransacNbInlierConsensus) {
    // Hold the list of the index of the inliers (points in the consensus set)
    std::vector<unsigned int> cur_consensus;
    // Hold the list of the index of the outliers
    std::vector<unsigned int> cur_outliers;
    // Hold the list of the index of the points randomly picked
    std::vector<unsigned int> cur_randoms;
    // Hold the list of the current inliers points to avoid to add a
    // degenerate point if the flag is set
    std::vector<vpPoint> cur_inliers;

    // Use a temporary variable because if not, the cMo passed in parameters
    // will be modified when
    // we compute the pose for the minimal sample sets but if the pose is not
    // correct when we pass a function pointer we do not want to modify the
    // cMo passed in parameters
    vpHomogeneousMatrix cMo_tmp;

    // Vector of used points, initialized at false for all points
    std::vector<bool> usedPt(size, false);

    vpPose poseMin;
    for (unsigned int i = 0; i < nbMinRandom;) {
      if ((size_t)std::count(usedPt.begin(), usedPt.end(), true) == usedPt.size()) {
        // All points was picked once, break otherwise we stay in an infinite loop
        break;
      }

      // Pick a point randomly
      unsigned int r_ = m_uniRand.uniform(0, size);

      while (usedPt[r_]) {
        // If already picked, pick another point randomly
        r_ = m_uniRand.uniform(0, size);
      }
      // Mark this point as already picked
      usedPt[r_] = true;
      vpPoint pt = m_listOfUniquePoints[r_];

      bool degenerate = false;
      if (m_checkDegeneratePoints) {
        if (std::find_if(poseMin.listOfPoints.begin(), poseMin.listOfPoints.end(), FindDegeneratePoint(pt)) !=
            poseMin.listOfPoints.end()) {
          degenerate = true;
        }
      }

      if (!degenerate) {
        poseMin.addPoint(pt);
        cur_randoms.push_back(r_);
        // Increment the number of points picked
        i++;
      }
    }

    if (poseMin.npt < nbMinRandom) {
      nbTrials++;
      continue;
    }

    bool is_pose_valid = false;
    double r_min = DBL_MAX;

    try {
      is_pose_valid = poseMin.computePose(vpPose::DEMENTHON_LAGRANGE_VIRTUAL_VS, cMo_tmp);
      r_min = poseMin.computeResidual(cMo_tmp);
    }
    catch (...) {
    }

    // If residual returned is not a number (NAN), set valid to false
    if (vpMath::isNaN(r_min)) {
      is_pose_valid = false;
    }

    // If at pose computation is OK we can continue, otherwise pick another random set
    if (is_pose_valid) {
      double r = sqrt(r_min) / (double)nbMinRandom; // FS should be r = sqrt(r_min / (double)nbMinRandom);
      // Filter the pose using some criterion (orientation angles,
      // translations, etc.)
      bool isPoseValid = true;
      if (m_func != nullptr) {
        isPoseValid = m_func(cMo_tmp);
        if (isPoseValid) {
          m_cMo = cMo_tmp;
        }
      }
      else {
     // No post filtering on pose, so copy cMo_temp to cMo
        m_cMo = cMo_tmp;
      }

      if (isPoseValid && r < m_ransacThreshold) {
        unsigned int nbInliersCur = 0;
        unsigned int iter = 0;
        for (std::vector<vpPoint>::const_iterator it = m_listOfUniquePoints.begin(); it != m_listOfUniquePoints.end();
             ++it, iter++) {
          p.setWorldCoordinates(it->get_oX(), it->get_oY(), it->get_oZ());
          p.track(m_cMo);

          double error = sqrt(vpMath::sqr(p.get_x() - it->get_x()) + vpMath::sqr(p.get_y() - it->get_y()));
          if (error < m_ransacThreshold) {
            bool degenerate = false;
            if (m_checkDegeneratePoints) {
              if (std::find_if(cur_inliers.begin(), cur_inliers.end(), FindDegeneratePoint(*it)) != cur_inliers.end()) {
                degenerate = true;
              }
            }

            if (!degenerate) {
              // the point is considered as inlier if the error is below the
              // threshold
              nbInliersCur++;
              cur_consensus.push_back(iter);
              cur_inliers.push_back(*it);
            }
            else {
              cur_outliers.push_back(iter);
            }
          }
          else {
            cur_outliers.push_back(iter);
          }
        }

        if (nbInliersCur > m_nbInliers) {
          foundSolution = true;
          m_best_consensus = cur_consensus;
          m_nbInliers = nbInliersCur;
        }

        nbTrials++;

        if (nbTrials >= m_ransacMaxTrials) {
          foundSolution = true;
        }
      }
      else {
        nbTrials++;
      }
    }
    else {
      nbTrials++;
    }
  }

  return foundSolution;
}

bool vpPose::poseRansac(vpHomogeneousMatrix &cMo, bool (*func)(const vpHomogeneousMatrix &))
{
  // Check only for adding / removing problem
  // Do not take into account problem with element modification here
  if (listP.size() != listOfPoints.size()) {
    std::cerr << "You should not modify vpPose::listP!" << std::endl;
    listOfPoints = std::vector<vpPoint>(listP.begin(), listP.end());
  }

  ransacInliers.clear();
  ransacInlierIndex.clear();

  std::vector<unsigned int> best_consensus;
  unsigned int nbInliers = 0;

  vpHomogeneousMatrix cMo_lagrange, cMo_dementhon;

  if (listOfPoints.size() < 4) {
    throw(vpPoseException(vpPoseException::notInitializedError, "Not enough point to compute the pose"));
  }

  std::vector<vpPoint> listOfUniquePoints;
  std::map<size_t, size_t> mapOfUniquePointIndex;

  // Get RANSAC flags
  bool prefilterDegeneratePoints = ransacFlag == PREFILTER_DEGENERATE_POINTS;
  bool checkDegeneratePoints = ransacFlag == CHECK_DEGENERATE_POINTS;

  if (prefilterDegeneratePoints) {
    // Remove degenerate object points
    std::map<vpPoint, size_t, CompareObjectPointDegenerate> filterObjectPointMap;
    size_t index_pt = 0;
    for (std::vector<vpPoint>::const_iterator it_pt = listOfPoints.begin(); it_pt != listOfPoints.end();
         ++it_pt, index_pt++) {
      if (filterObjectPointMap.find(*it_pt) == filterObjectPointMap.end()) {
        filterObjectPointMap[*it_pt] = index_pt;
      }
    }

    std::map<vpPoint, size_t, CompareImagePointDegenerate> filterImagePointMap;
    for (std::map<vpPoint, size_t, CompareObjectPointDegenerate>::const_iterator it = filterObjectPointMap.begin();
         it != filterObjectPointMap.end(); ++it) {
      if (filterImagePointMap.find(it->first) == filterImagePointMap.end()) {
        filterImagePointMap[it->first] = it->second;

        listOfUniquePoints.push_back(it->first);
        mapOfUniquePointIndex[listOfUniquePoints.size() - 1] = it->second;
      }
    }
  }
  else {
 // No prefiltering
    listOfUniquePoints = listOfPoints;

    size_t index_pt = 0;
    for (std::vector<vpPoint>::const_iterator it_pt = listOfPoints.begin(); it_pt != listOfPoints.end();
         ++it_pt, index_pt++) {
      mapOfUniquePointIndex[index_pt] = index_pt;
    }
  }

  if (listOfUniquePoints.size() < 4) {
    throw(vpPoseException(vpPoseException::notInitializedError, "Not enough point to compute the pose"));
  }

  unsigned int nbThreads = 1;
  bool executeParallelVersion = useParallelRansac;

  if (executeParallelVersion) {
    if (nbParallelRansacThreads <= 0) {
      // Get number of CPU threads
      nbThreads = std::thread::hardware_concurrency();
      if (nbThreads <= 1) {
        nbThreads = 1;
        executeParallelVersion = false;
      }
    }
    else {
      nbThreads = nbParallelRansacThreads;
    }
  }

  bool foundSolution = false;

  if (executeParallelVersion) {
    std::vector<std::thread> threadpool;
    std::vector<vpRansacFunctor> ransacWorkers;

    int splitTrials = ransacMaxTrials / nbThreads;
    for (size_t i = 0; i < (size_t)nbThreads; i++) {
      unsigned int initial_seed = (unsigned int)i; //((unsigned int) time(nullptr) ^ i);
      if (i < (size_t)nbThreads - 1) {
        ransacWorkers.emplace_back(cMo, ransacNbInlierConsensus, splitTrials, ransacThreshold, initial_seed,
                                   checkDegeneratePoints, listOfUniquePoints, func);
      }
      else {
        int maxTrialsRemainder = ransacMaxTrials - splitTrials * (nbThreads - 1);
        ransacWorkers.emplace_back(cMo, ransacNbInlierConsensus, maxTrialsRemainder, ransacThreshold, initial_seed,
                                   checkDegeneratePoints, listOfUniquePoints, func);
      }
    }

    for (auto &worker : ransacWorkers) {
      threadpool.emplace_back(&vpRansacFunctor::operator(), &worker);
    }

    for (auto &th : threadpool) {
      th.join();
    }

    bool successRansac = false;
    size_t best_consensus_size = 0;
    for (auto &worker : ransacWorkers) {
      if (worker.getResult()) {
        successRansac = true;

        if (worker.getBestConsensus().size() > best_consensus_size) {
          nbInliers = worker.getNbInliers();
          best_consensus = worker.getBestConsensus();
          best_consensus_size = worker.getBestConsensus().size();
        }
      }
    }

    foundSolution = successRansac;
  }
  else {
 // Sequential RANSAC
    vpRansacFunctor sequentialRansac(cMo, ransacNbInlierConsensus, ransacMaxTrials, ransacThreshold, 0,
                                   checkDegeneratePoints, listOfUniquePoints, func);
    sequentialRansac();
    foundSolution = sequentialRansac.getResult();

    if (foundSolution) {
      nbInliers = sequentialRansac.getNbInliers();
      best_consensus = sequentialRansac.getBestConsensus();
    }
  }

  if (foundSolution) {
    unsigned int nbMinRandom = 4;
    //    std::cout << "Nombre d'inliers " << nbInliers << std::endl ;

    // Display the random picked points
    /*
    std::cout << "Randoms : ";
    for(unsigned int i = 0 ; i < cur_randoms.size() ; i++)
      std::cout << cur_randoms[i] << " ";
    std::cout << std::endl;
    */

    // Display the outliers
    /*
    std::cout << "Outliers : ";
    for(unsigned int i = 0 ; i < cur_outliers.size() ; i++)
      std::cout << cur_outliers[i] << " ";
    std::cout << std::endl;
    */

    // Even if the cardinality of the best consensus set is inferior to
    // ransacNbInlierConsensus,  we want to refine the solution with data in
    // best_consensus and return this pose.  This is an approach used for
    // example in p118 in Multiple View Geometry in Computer Vision, Hartley,
    // R.~I. and Zisserman, A.
    if (nbInliers >= nbMinRandom) // if(nbInliers >= (unsigned)ransacNbInlierConsensus)
    {
      // Refine the solution using all the points in the consensus set and
      // with VVS pose estimation
      vpPose pose;
      for (size_t i = 0; i < best_consensus.size(); i++) {
        vpPoint pt = listOfUniquePoints[best_consensus[i]];

        pose.addPoint(pt);
        ransacInliers.push_back(pt);
      }

      // Update the list of inlier index
      for (std::vector<unsigned int>::const_iterator it_index = best_consensus.begin();
           it_index != best_consensus.end(); ++it_index) {
        ransacInlierIndex.push_back((unsigned int)mapOfUniquePointIndex[*it_index]);
      }

      pose.setCovarianceComputation(computeCovariance);
      pose.computePose(vpPose::DEMENTHON_LAGRANGE_VIRTUAL_VS, cMo);

      // In some rare cases, the final pose could not respect the pose
      // criterion even  if the 4 minimal points picked respect the pose
      // criterion.
      if (func != nullptr && !func(cMo)) {
        return false;
      }

      if (computeCovariance) {
        covarianceMatrix = pose.covarianceMatrix;
      }
    }
    else {
      return false;
    }
  }

  return foundSolution;
}

int vpPose::computeRansacIterations(double probability, double epsilon, const int sampleSize, int maxIterations)
{
  probability = std::max<double>(probability, 0.0);
  probability = std::min<double>(probability, 1.0);
  epsilon = std::max<double>(epsilon, 0.0);
  epsilon = std::min<double>(epsilon, 1.0);

  if (vpMath::nul(epsilon)) {
    // no outliers
    return 1;
  }

  if (maxIterations <= 0) {
    maxIterations = std::numeric_limits<int>::max();
  }

  double logarg, logval, N;
  logarg = -std::pow(1.0 - epsilon, sampleSize);
#ifdef VISP_HAVE_FUNC_LOG1P
  logval = log1p(logarg);
#else
  logval = log(1.0 + logarg);
#endif
  if (vpMath::nul(logval, std::numeric_limits<double>::epsilon())) {
    std::cerr << "vpMath::nul(log(1.0 - std::pow(1.0 - epsilon, "
      "sampleSize)), std::numeric_limits<double>::epsilon())"
      << std::endl;
    return 0;
  }

  N = log(std::max<double>(1.0 - probability, std::numeric_limits<double>::epsilon())) / logval;
  if (logval < 0.0 && N < maxIterations) {
    return (int)ceil(N);
  }

  return maxIterations;
}

void vpPose::findMatch(std::vector<vpPoint> &p2D, std::vector<vpPoint> &p3D,
                       const unsigned int &numberOfInlierToReachAConsensus, const double &threshold,
                       unsigned int &ninliers, std::vector<vpPoint> &listInliers, vpHomogeneousMatrix &cMo,
                       const int &maxNbTrials, bool useParallelRansac, unsigned int nthreads,
                       bool (*func)(const vpHomogeneousMatrix &))
{
  vpPose pose;

  for (unsigned int i = 0; i < p2D.size(); i++) {
    for (unsigned int j = 0; j < p3D.size(); j++) {
      vpPoint pt(p3D[j].getWorldCoordinates());
      pt.set_x(p2D[i].get_x());
      pt.set_y(p2D[i].get_y());
      pose.addPoint(pt);
    }
  }

  if (pose.listP.size() < 4) {
    vpERROR_TRACE("Ransac method cannot be used in that case ");
    vpERROR_TRACE("(at least 4 points are required)");
    vpERROR_TRACE("Not enough point (%d) to compute the pose  ", pose.listP.size());
    throw(vpPoseException(vpPoseException::notEnoughPointError, "Not enough point (%d) to compute the pose by ransac",
                          pose.listP.size()));
  }
  else {
    pose.setUseParallelRansac(useParallelRansac);
    pose.setNbParallelRansacThreads(nthreads);
    // Since we add duplicate points, we need to check for degenerate configuration
    pose.setRansacFilterFlag(vpPose::CHECK_DEGENERATE_POINTS);
    pose.setRansacMaxTrials(maxNbTrials);
    pose.setRansacNbInliersToReachConsensus(numberOfInlierToReachAConsensus);
    pose.setRansacThreshold(threshold);
    pose.computePose(vpPose::RANSAC, cMo, func);
    ninliers = pose.getRansacNbInliers();
    listInliers = pose.getRansacInliers();
  }
}
