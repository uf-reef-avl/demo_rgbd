#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ros/ros.h>

#include <nav_msgs/Odometry.h>

#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

#include "cameraParameters.h"
#include "pointDefinition.h"

const double PI = 3.1415926;

const int imagePixelNum = imageHeight * imageWidth;

const int keepSyncCloudNum = 15;
double syncCloudTime[keepSyncCloudNum] = {0};
pcl::PointCloud<pcl::PointXYZ>::Ptr syncCloudArray[keepSyncCloudNum];
int syncCloudInd = -1;
int cloudRegInd = 0;

pcl::PointCloud<pcl::PointXYZ>::Ptr surroundCloud(new pcl::PointCloud<pcl::PointXYZ>());
pcl::PointCloud<pcl::PointXYZ>::Ptr tempCloud(new pcl::PointCloud<pcl::PointXYZ>());
pcl::PointCloud<pcl::PointXYZ>::Ptr tempCloud2(new pcl::PointCloud<pcl::PointXYZ>());

double timeRec = 0;
double rxRec = 0, ryRec = 0, rzRec = 0;
double txRec = 0, tyRec = 0, tzRec = 0;

int cloudCount = -1;
const int cloudSkipNum = 5;

int showCount = -1;
const int showSkipNum = 10;

ros::Publisher *surroundCloudPubPointer = NULL;

void voDataHandler(const nav_msgs::Odometry::ConstPtr& voData)
{
  double time = voData->header.stamp.toSec();

  double rx, ry, rz;
  geometry_msgs::Quaternion geoQuat = voData->pose.pose.orientation;
  tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w)).getRPY(rz, rx, ry);

  double tx = voData->pose.pose.position.x;
  double ty = voData->pose.pose.position.y;
  double tz = voData->pose.pose.position.z;

  if (time - timeRec < 0.5 && syncCloudInd >= 0) {
    pcl::PointXYZ point;
    while (syncCloudTime[cloudRegInd] <= time && cloudRegInd != (syncCloudInd + 1) % keepSyncCloudNum) {
      double scaleLast =  (time - syncCloudTime[cloudRegInd]) / (time - timeRec);
      double scaleCur =  (syncCloudTime[cloudRegInd] - timeRec) / (time - timeRec);
      if (scaleLast > 1) {
        scaleLast = 1;
      } else if (scaleLast < 0) {
        scaleLast = 0;
      }
      if (scaleCur > 1) {
        scaleCur = 1;
      } else if (scaleCur < 0) {
        scaleCur = 0;
      }

      double rx2 = rx * scaleCur + rxRec * scaleLast;
      double ry2;
      if (ry - ryRec > PI) {
        ry2 = ry * scaleCur + (ryRec + 2 * PI) * scaleLast;
      } else if (ry - ryRec < -PI) {
        ry2 = ry * scaleCur + (ryRec - 2 * PI) * scaleLast;
      } else {
        ry2 = ry * scaleCur + ryRec * scaleLast;
      }
      double rz2 = rz * scaleCur + rzRec * scaleLast;

      double tx2 = tx * scaleCur + txRec * scaleLast;
      double ty2 = ty * scaleCur + tyRec * scaleLast;
      double tz2 = tz * scaleCur + tzRec * scaleLast;

      double cosrx2 = cos(rx2);
      double sinrx2 = sin(rx2);
      double cosry2 = cos(ry2);
      double sinry2 = sin(ry2);
      double cosrz2 = cos(rz2);
      double sinrz2 = sin(rz2);

      pcl::PointCloud<pcl::PointXYZ>::Ptr syncCloudPointer = syncCloudArray[cloudRegInd];
      int syncCloudNum = syncCloudPointer->points.size();
      for (int i = 0; i < syncCloudNum; i++) {
        point = syncCloudPointer->points[i];
        double pointDis = sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
        if (pointDis > 0.3 && pointDis < 5) {
          double x1 = cosrz2 * point.x - sinrz2 * point.y;
          double y1 = sinrz2 * point.x + cosrz2 * point.y;
          double z1 = point.z;

          double x2 = x1;
          double y2 = cosrx2 * y1 + sinrx2 * z1;
          double z2 = -sinrx2 * y1 + cosrx2 * z1;

          point.x = cosry2 * x2 - sinry2 * z2 + tx2;
          point.y = y2 + ty2;
          point.z = sinry2 * x2 + cosry2 * z2 + tz2;

          tempCloud->push_back(point);
        }
      }

      cloudRegInd = (cloudRegInd + 1) % keepSyncCloudNum;
    }

    showCount = (showCount + 1) % (showSkipNum + 1);
    if (showCount != 0) {
      rxRec = rx; ryRec = ry; rzRec = rz;
      txRec = tx; tyRec = ty; tzRec = tz;
      timeRec = time;
      return;
    }

    int surroundCloudNum = surroundCloud->points.size();
    for (int i = 0; i < surroundCloudNum; i++) {
      point = surroundCloud->points[i];

      double xDiff = point.x - tx;
      double yDiff = point.y - ty;
      double zDiff = point.z - tz;

      double pointDis = sqrt(xDiff * xDiff + yDiff * yDiff + zDiff * zDiff);
      if (pointDis < 50) {
        tempCloud->push_back(point);
      }
    }

    surroundCloud->clear();
    pcl::VoxelGrid<pcl::PointXYZ> downSizeFilter;
    downSizeFilter.setInputCloud(tempCloud);
    downSizeFilter.setLeafSize(0.2, 0.2, 0.2);
    downSizeFilter.filter(*surroundCloud);
    tempCloud->clear();

    sensor_msgs::PointCloud2 surroundCloud2;
    pcl::toROSMsg(*surroundCloud, surroundCloud2);
    surroundCloud2.header.frame_id = "camera_init";
    surroundCloud2.header.stamp = voData->header.stamp;
    surroundCloudPubPointer->publish(surroundCloud2);
  }

  rxRec = rx; ryRec = ry; rzRec = rz;
  txRec = tx; tyRec = ty; tzRec = tz;
  timeRec = time;
}

void syncCloudHandler(const sensor_msgs::Image::ConstPtr& syncCloud2)
{
  cloudCount = (cloudCount + 1) % (cloudSkipNum + 1);
  if (cloudCount != 0) {
    return;
  }

  double time = syncCloud2->header.stamp.toSec();

  syncCloudInd = (syncCloudInd + 1) % keepSyncCloudNum;
  syncCloudTime[syncCloudInd] = time;

  tempCloud2->clear();
  pcl::PointXYZ point;
  const float* syncCloud2Pointer = reinterpret_cast<const float*>(&syncCloud2->data[0]);
  for (int i = 0; i < imagePixelNum; i++) {
    float val = syncCloud2Pointer[i];

    int xd = i % imageWidth;
    int yd = int(i / imageWidth);

    double ud = (kDepth[2] - xd) / kDepth[0];
    double vd = (kDepth[5] - yd) / kDepth[4];

    point.z = val;
    point.x = ud * val;
    point.y = vd * val;

    if (point.z > 0.3 && point.z < 7) {
      tempCloud2->push_back(point);
    }
  }

  pcl::PointCloud<pcl::PointXYZ>::Ptr syncCloudPointer = syncCloudArray[syncCloudInd];
  syncCloudPointer->clear();

  pcl::VoxelGrid<pcl::PointXYZ> downSizeFilter;
  downSizeFilter.setInputCloud(tempCloud2);
  downSizeFilter.setLeafSize(0.1, 0.1, 0.1);
  downSizeFilter.filter(*syncCloudPointer);
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "registerPointCloud");
  ros::NodeHandle nh;

  for (int i = 0; i < keepSyncCloudNum; i++) {
    pcl::PointCloud<pcl::PointXYZ>::Ptr syncCloudTemp(new pcl::PointCloud<pcl::PointXYZ>());
    syncCloudArray[i] = syncCloudTemp;
  }

  ros::Subscriber voDataSub = nh.subscribe<nav_msgs::Odometry> ("cam2_to_init", 5, voDataHandler);

  ros::Subscriber syncCloudSub = nh.subscribe<sensor_msgs::Image>
                                 ("/camera/depth_registered/image", 1, syncCloudHandler);

  ros::Publisher surroundCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("surround_cloud", 1);
  surroundCloudPubPointer = &surroundCloudPub;

  ros::spin();

  return 0;
}
