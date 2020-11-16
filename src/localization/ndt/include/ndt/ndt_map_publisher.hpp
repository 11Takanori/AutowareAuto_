// Copyright 2020 the Autoware Foundation
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

#ifndef NDT__NDT_MAP_PUBLISHER_HPP_
#define NDT__NDT_MAP_PUBLISHER_HPP_

#include <ndt/ndt_map.hpp>
#include <ndt/visibility_control.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <voxel_grid_nodes/algorithm/voxel_cloud_centroid.hpp>

#include <string>

using autoware::common::types::float64_t;

namespace autoware
{
namespace localization
{
namespace ndt
{

/// struct to hold geodetic pose of map origin
/// decribed as a latitude, longitude, and elevation
/// along with an orientation: roll, pitch and yaw
struct geodetic_pose_t
{
  float64_t latitude;
  float64_t longitude;
  float64_t elevation;
  float64_t roll;
  float64_t pitch;
  float64_t yaw;
};

/// struct to hold geocentric pose of map origin
/// decribed as a x, y, z
/// along with an orientation: roll, pitch and yaw
struct geocentric_pose_t
{
  float64_t x;
  float64_t y;
  float64_t z;
  float64_t roll;
  float64_t pitch;
  float64_t yaw;
};

/// Read the map info from a yaml file. Throws if the file cannot be read.
/// \param[in] yaml_file_name Name of the ymal file.
/// \param[out] geo_pose Geocentric pose describing map orgin
void NDT_PUBLIC read_from_yaml(
  const std::string & yaml_file_name,
  geodetic_pose_t * geo_pose);

/// Read the pcd file into a PointCloud2 message. Throws if the file cannot be read.
/// \param[in] file_name Name of the pcd file.
/// \param[out] msg Pointer to PointCloud2 message
void NDT_PUBLIC read_from_pcd(
  const std::string & file_name,
  sensor_msgs::msg::PointCloud2 * msg);

class NDT_PUBLIC NDTMapPublisher
{
public:
  using MapConfig = perception::filters::voxel_grid::Config;
  NDTMapPublisher(
    const MapConfig & map_config,
    sensor_msgs::msg::PointCloud2 & map_pc,
    sensor_msgs::msg::PointCloud2 & source_pc);

  /// Read the pcd file with filename into a PointCloud2 message, transform it into an NDT
  /// representation and then serialize the ndt representation back into a PointCloud2 message
  /// that can be published.
  /// \param yaml_file_name File name of the yaml file.
  /// \param pcl_file_name File name of the pcd file.
  /// \return The geocentric position.
  geocentric_pose_t load_map(const std::string & yaml_file_name, const std::string & pcl_file_name);

  /// Iterate over the map representation and convert it into a PointCloud2 message where each voxel
  /// in the map corresponds to a single point in the PointCloud2 field. See the documentation for
  /// the specs and the format of the point cloud message.
  void map_to_pc(const ndt::DynamicNDTMap & ndt_map);

  /// Convenience function to clear the contents of a pointcloud message.
  /// Can be removed when #102 is merged in.
  void reset_pc_msg(sensor_msgs::msg::PointCloud2 & msg);

private:
  const MapConfig & m_map_config;
  sensor_msgs::msg::PointCloud2 & m_map_pc;
  sensor_msgs::msg::PointCloud2 & m_source_pc;
};

}  // namespace ndt
}  // namespace localization
}  // namespace autoware

#endif  // NDT__NDT_MAP_PUBLISHER_HPP_
