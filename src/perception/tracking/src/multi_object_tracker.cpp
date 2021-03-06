// Copyright 2021 The Autoware Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Co-developed by Tier IV, Inc. and Apex.AI, Inc.

#include "tracking/multi_object_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "autoware_auto_tf2/tf2_autoware_auto_msgs.hpp"
#include "geometry_msgs/msg/quaternion.hpp"
#include "tf2_eigen/tf2_eigen.h"
#include "time_utils/time_utils.hpp"

using autoware::common::types::float64_t;

namespace autoware
{
namespace perception
{
namespace tracking
{
namespace
{

bool is_gravity_aligned(const geometry_msgs::msg::Quaternion & quat)
{
  // Check that the transformation is still roughly 2D, i.e. does not have substantial pitch and
  // roll. That means that either the rotation angle is small, or the rotation axis is
  // approximately equal to the z axis.
  constexpr float64_t kAngleThresh = 0.1;  // rad
  constexpr float64_t kAxisTiltThresh = 0.1;  // rad
  // rotation angle small
  // ⇔ |θ| <= kAngleThresh  (angles are assumed to be between -π and π)
  // ⇔ cos(θ/2) => std::cos(kAngleThresh/2)
  // ⇔ w => std::cos(kAngleThresh/2)
  if (quat.w < std::cos(0.5 * kAngleThresh)) {
    // From Wikipedia: (x, y, z) = cos(θ/2) * (u_x, u_y, u_z), where u is the rotation axis.
    // The cosine of the angle α between the rotation axis and the z axis is the dot product of the
    // rotation axis u and the the z axis, so cos(α) = u_z.
    const float64_t u_z = std::abs(quat.z) / std::sqrt(
      quat.x * quat.x + quat.y * quat.y + quat.z * quat.z);
    if (u_z < std::cos(kAxisTiltThresh)) {
      return false;
    }
  }
  return true;
}

geometry_msgs::msg::TransformStamped to_transform(const nav_msgs::msg::Odometry & odometry)
{
  geometry_msgs::msg::TransformStamped tfs;
  tfs.header = odometry.header;
  tfs.child_frame_id = odometry.child_frame_id;
  tfs.transform.translation.x = odometry.pose.pose.position.x;
  tfs.transform.translation.y = odometry.pose.pose.position.y;
  tfs.transform.translation.z = odometry.pose.pose.position.z;
  tfs.transform.rotation = odometry.pose.pose.orientation;
  return tfs;
}
}  // anonymous namespace


MultiObjectTracker::MultiObjectTracker(MultiObjectTrackerOptions options)
: m_options(options), m_object_associator(options.object_association_config),
  m_vision_associator{options.vision_association_config},
  m_track_creator(options.track_creator_config) {}

TrackerUpdateResult MultiObjectTracker::update(
  DetectedObjectsMsg detections,
  const nav_msgs::msg::Odometry & detection_frame_odometry)
{
  TrackerUpdateResult result;
  result.status = this->validate(detections, detection_frame_odometry);
  if (result.status != TrackerUpdateStatus::Ok) {
    return result;
  }

  // ==================================
  // Transform detections
  // ==================================
  this->transform(detections, detection_frame_odometry);

  // ==================================
  // Predict tracks forward
  // ==================================
  // TODO(nikolai.morin): Simplify after #1002
  const auto target_time = time_utils::from_message(detections.header.stamp);
  const auto dt = target_time - m_last_update;
  for (auto & object : m_objects) {
    object.predict(dt);
  }

  // ==================================
  // Associate observations with tracks
  // ==================================
  AssociatorResult association;
  association = m_object_associator.assign(detections, this->m_objects);
  if (association.had_errors) {
    result.status = TrackerUpdateStatus::InvalidShape;
  }

  // ==================================
  // Update tracks with observations
  // ==================================
  for (size_t track_idx = 0; track_idx < m_objects.size(); ++track_idx) {
    size_t detection_idx = association.track_assignments[track_idx];
    if (detection_idx == AssociatorResult::UNASSIGNED) {
      continue;
    }
    const auto & detection = detections.objects[detection_idx];
    m_objects[track_idx].update(detection);
  }
  for (const size_t track_idx : association.unassigned_track_indices) {
    m_objects[track_idx].no_update();
  }

  // ==================================
  // Initialize new tracks
  // ==================================
  m_track_creator.add_objects(detections, association);
  {
    auto && ret = m_track_creator.create_tracks();
    m_objects.insert(
      m_objects.end(),
      std::make_move_iterator(ret.tracks.begin()),
      std::make_move_iterator(ret.tracks.end()));
  }

  // ==================================
  // Prune tracks
  // ==================================
  const auto last = std::remove_if(
    m_objects.begin(), m_objects.end(), [this](const auto & object) {
      return object.should_be_removed(
        this->m_options.pruning_time_threshold,
        this->m_options.pruning_ticks_threshold);
    });
  m_objects.erase(last, m_objects.end());

  // ==================================
  // Build result
  // ==================================
  result.objects =
    std::make_unique<TrackedObjectsMsg>(this->convert_to_msg(detections.header.stamp));
  result.status = TrackerUpdateStatus::Ok;
  m_last_update = target_time;

  return result;
}

void MultiObjectTracker::update(
  const ClassifiedRoiArrayMsg & rois,
  const geometry_msgs::msg::Transform & tf_camera_from_track)
{
  const auto association = m_vision_associator.assign(rois, m_objects, tf_camera_from_track);

  for (size_t i = 0U; i < m_objects.size(); ++i) {
    const auto & maybe_roi_idx = association.track_assignments[i];
    if (maybe_roi_idx != AssociatorResult::UNASSIGNED) {
      m_objects[i].update(rois.rois[maybe_roi_idx].classifications);
    }
  }
  m_track_creator.add_objects(rois, association);
}

TrackerUpdateStatus MultiObjectTracker::validate(
  const DetectedObjectsMsg & detections,
  const nav_msgs::msg::Odometry & detection_frame_odometry)
{
  const auto target_time = time_utils::from_message(detections.header.stamp);
  if (target_time < m_last_update) {
    return TrackerUpdateStatus::WentBackInTime;
  }
  if (detections.header.frame_id != detection_frame_odometry.child_frame_id) {
    return TrackerUpdateStatus::DetectionFrameMismatch;
  }
  if (detection_frame_odometry.header.frame_id != m_options.frame) {
    return TrackerUpdateStatus::TrackerFrameMismatch;
  }
  if (!is_gravity_aligned(detection_frame_odometry.pose.pose.orientation)) {
    return TrackerUpdateStatus::FrameNotGravityAligned;
  }
  // Could also validate
  // * classes
  // * object shapes
  // * detection poses are gravity aligned
  return TrackerUpdateStatus::Ok;
}

void MultiObjectTracker::transform(
  DetectedObjectsMsg & detections,
  const nav_msgs::msg::Odometry & detection_frame_odometry)
{
  // Convert the odometry to Eigen objects.
  Eigen::Isometry3d tf__tracking__detection = Eigen::Isometry3d::Identity();
  tf2::fromMsg(detection_frame_odometry.pose.pose, tf__tracking__detection);
  const Eigen::Matrix3d rot_d = tf__tracking__detection.linear();
  // Convert the odometry to TransformStamped for use with tf2::doTransform.
  const geometry_msgs::msg::TransformStamped tf_msg__tracking__detection = to_transform(
    detection_frame_odometry);
  // Hoisted outside the loop
  Eigen::Vector3d centroid_detection = Eigen::Vector3d::Zero();
  Eigen::Vector3d centroid_tracking = Eigen::Vector3d::Zero();

  detections.header.frame_id = m_options.frame;
  for (auto & detection : detections.objects) {
    // Transform the shape. If needed, this can potentially be made more efficient by not using
    // tf2::doTransform, which converts the TransformStamped message to a different representation
    // in each call.
    tf2::doTransform(detection.shape.polygon, detection.shape.polygon, tf_msg__tracking__detection);
    // Transform the pose.
    tf2::fromMsg(detection.kinematics.centroid_position, centroid_detection);
    centroid_tracking = tf__tracking__detection * centroid_detection;
    detection.kinematics.centroid_position = tf2::toMsg(centroid_tracking);
    if (detection.kinematics.has_position_covariance) {
      // Doing this properly is difficult. We'll ignore the rotational part. This is a practical
      // solution since only the yaw covariance is relevant, and the yaw covariance is
      // unaffected by the transformation, which preserves the z axis.
      // An even more accurate implementation could additionally include the odometry covariance.
      Eigen::Map<Eigen::Matrix<double, 3, 3, Eigen::RowMajor>> cov(
        detection.kinematics.position_covariance.data());
      cov = rot_d * cov * rot_d.transpose();
    }
    // Transform the twist.
    if (detection.kinematics.has_twist) {
      auto & linear = detection.kinematics.twist.twist.linear;
      const auto & frame_linear = detection_frame_odometry.twist.twist.linear;
      const Eigen::Vector3d eigen_linear{linear.x, linear.y, linear.z};
      const Eigen::Vector3d eigen_linear_transformed = rot_d * eigen_linear;
      // This assumes the detection frame has no angular velocity wrt the tracking frame.
      // TODO(nikolai.morin): Implement the full formula, to be found in
      // Craig's "Introduction to robotics" book, third edition, formula 5.13
      linear.x = frame_linear.x + eigen_linear_transformed.x();
      linear.y = frame_linear.y + eigen_linear_transformed.y();
      linear.z = frame_linear.z + eigen_linear_transformed.z();
    }
  }
}

MultiObjectTracker::TrackedObjectsMsg MultiObjectTracker::convert_to_msg(
  const builtin_interfaces::msg::Time & stamp) const
{
  TrackedObjectsMsg array;
  array.header.stamp = stamp;
  array.header.frame_id = m_options.frame;
  array.objects.reserve(m_objects.size());
  std::transform(
    m_objects.begin(), m_objects.end(), std::back_inserter(array.objects), [](
      TrackedObject o) {return o.msg();});
  return array;
}


}  // namespace tracking
}  // namespace perception
}  // namespace autoware
