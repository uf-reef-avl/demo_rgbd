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

const int keepSyncCloudNum = 5;
double syncCloudTime[keepSyncCloudNum] = {0};
pcl::PointCloud<pcl::PointXYZI>::Ptr syncCloudArray[keepSyncCloudNum];
int syncCloudInd = -1;
int cloudRegInd = 0;

pcl::PointCloud<pcl::PointXYZI>::Ptr depthCloud(new pcl::PointCloud<pcl::PointXYZI>());
pcl::PointCloud<pcl::PointXYZI>::Ptr tempCloud(new pcl::PointCloud<pcl::PointXYZI>());
pcl::PointCloud<pcl::PointXYZI>::Ptr tempCloud2(new pcl::PointCloud<pcl::PointXYZI>());
pcl::PointCloud<pcl::PointXYZI>::Ptr tempCloud3(new pcl::PointCloud<pcl::PointXYZI>());

double timeRec = 0;
double rxRec = 0, ryRec = 0, rzRec = 0;
double txRec = 0, tyRec = 0, tzRec = 0;

bool systemInited = false;
double initTime;

int cloudCount = -1;
const int cloudSkipNum = 5;
const int cloudDSRate = 5;

ros::Publisher *depthCloudPubPointer = NULL;

void voDataHandler(const nav_msgs::Odometry::ConstPtr& voData)
{
  double time = voData->header.stamp.toSec();

  double roll, pitch, yaw;
  geometry_msgs::Quaternion geoQuat = voData->pose.pose.orientation;
  tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w)).getRPY(roll, pitch, yaw);

  double rx = voData->twist.twist.angular.x - rxRec;
  double ry = voData->twist.twist.angular.y - ryRec;
  double rz = voData->twist.twist.angular.z - rzRec;

  if (ry < -PI) {
    ry += 2 * PI;
  } else if (ry > PI) {
    ry -= 2 * PI;
  }

  double tx = voData->pose.pose.position.x - txRec;
  double ty = voData->pose.pose.position.y - tyRec;
  double tz = voData->pose.pose.position.z - tzRec;

  rxRec = voData->twist.twist.angular.x;
  ryRec = voData->twist.twist.angular.y;
  rzRec = voData->twist.twist.angular.z;

  txRec = voData->pose.pose.position.x;
  tyRec = voData->pose.pose.position.y;
  tzRec = voData->pose.pose.position.z;

  double x1 = cos(yaw) * tx + sin(yaw) * tz;
  double y1 = ty;
  double z1 = -sin(yaw) * tx + cos(yaw) * tz;

  double x2 = x1;
  double y2 = cos(pitch) * y1 - sin(pitch) * z1;
  double z2 = sin(pitch) * y1 + cos(pitch) * z1;

  tx = cos(roll) * x2 + sin(roll) * y2;
  ty = -sin(roll) * x2 + cos(roll) * y2;
  tz = z2;

  double cosrx = cos(rx);
  double sinrx = sin(rx);
  double cosry = cos(ry);
  double sinry = sin(ry);
  double cosrz = cos(rz);
  double sinrz = sin(rz);

  if (time - timeRec < 0.5 && syncCloudInd >= 0) {
    pcl::PointXYZI point;
    tempCloud->clear();
    double x1, y1, z1, x2, y2, z2;
    int depthCloudNum = depthCloud->points.size();
    for (int i = 0; i < depthCloudNum; i++) {
      point = depthCloud->points[i];

      x1 = cosry * point.x - sinry * point.z;
      y1 = point.y;
      z1 = sinry * point.x + cosry * point.z;

      x2 = x1;
      y2 = cosrx * y1 + sinrx * z1;
      z2 = -sinrx * y1 + cosrx * z1;

      point.x = cosrz * x2 + sinrz * y2 - tx;
      point.y = -sinrz * x2 + cosrz * y2 - ty;
      point.z = z2 - tz;

      double pointDis = sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
      double timeDis = time - initTime - point.intensity;
      if (fabs(point.x / point.z) < 2 && fabs(point.y / point.z) < 1 && point.z > 0.3 && pointDis < 15 &&  
          timeDis <= 5.0) {
        tempCloud->push_back(point);
      }
    }

    while (syncCloudTime[cloudRegInd] <= time && cloudRegInd != (syncCloudInd + 1) % keepSyncCloudNum) {
      double scale = (time - syncCloudTime[cloudRegInd]) / (time - timeRec);
      if (scale > 1) {
        scale = 1;
      } else if (scale < 0) {
        scale = 0;
      }

      double rx2 = rx * scale;
      double ry2 = ry * scale;
      double rz2 = rz * scale;

      double tx2 = tx * scale;
      double ty2 = ty * scale;
      double tz2 = tz * scale;

      double cosrx2 = cos(rx2);
      double sinrx2 = sin(rx2);
      double cosry2 = cos(ry2);
      double sinry2 = sin(ry2);
      double cosrz2 = cos(rz2);
      double sinrz2 = sin(rz2);

      pcl::PointCloud<pcl::PointXYZI>::Ptr syncCloudPointer = syncCloudArray[cloudRegInd];
      int syncCloudNum = syncCloudPointer->points.size();
      for (int i = 0; i < syncCloudNum; i++) {
        point = syncCloudPointer->points[i];

        x1 = cosry2 * point.x - sinry2 * point.z;
        y1 = point.y;
        z1 = sinry2 * point.x + cosry2 * point.z;

        x2 = x1;
        y2 = cosrx2 * y1 + sinrx2 * z1;
        z2 = -sinrx2 * y1 + cosrx2 * z1;

        point.x = cosrz2 * x2 + sinrz2 * y2 - tx2;
        point.y = -sinrz2 * x2 + cosrz2 * y2 - ty2;
        point.z = z2 - tz2;

        double pointDis = sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
        if (fabs(point.x / point.z) < 2 && fabs(point.y / point.z) < 1.5 && point.z > 0.3 && 
            pointDis < 15) {
          tempCloud->push_back(point);
        }
      }
      cloudRegInd = (cloudRegInd + 1) % keepSyncCloudNum;
    }

    depthCloud->clear();
    pcl::VoxelGrid<pcl::PointXYZI> downSizeFilter;
    downSizeFilter.setInputCloud(tempCloud);
    downSizeFilter.setLeafSize(0.05, 0.05, 0.05);
    downSizeFilter.filter(*depthCloud);
    depthCloudNum = depthCloud->points.size();

    tempCloud->clear();
    for (int i = 0; i < depthCloudNum; i++) {
      point = depthCloud->points[i];

      if (fabs(point.x / point.z) < 1 && fabs(point.y / point.z) < 0.6) {
        point.intensity = depthCloud->points[i].z;
        point.x *= 10 / depthCloud->points[i].z;
        point.y *= 10 / depthCloud->points[i].z;
        point.z = 10;

        tempCloud->push_back(point);
      }
    }

    tempCloud2->clear();
    downSizeFilter.setInputCloud(tempCloud);
    downSizeFilter.setLeafSize(0.1, 0.1, 0.1);
    downSizeFilter.filter(*tempCloud2);
    int tempCloud2Num = tempCloud2->points.size();

    for (int i = 0; i < tempCloud2Num; i++) {
      tempCloud2->points[i].z = tempCloud2->points[i].intensity;
      tempCloud2->points[i].x *= tempCloud2->points[i].z / 10;
      tempCloud2->points[i].y *= tempCloud2->points[i].z / 10;
      tempCloud2->points[i].intensity = 10;
    }

    sensor_msgs::PointCloud2 depthCloud2;
    pcl::toROSMsg(*tempCloud2, depthCloud2);
    depthCloud2.header.frame_id = "camera2";
    depthCloud2.header.stamp = voData->header.stamp;
    depthCloudPubPointer->publish(depthCloud2);
  }

  timeRec = time;
}

void syncCloudHandler(const sensor_msgs::Image::ConstPtr& syncCloud2)
{
  cloudCount = (cloudCount + 1) % (cloudSkipNum + 1);
  if (cloudCount != 0) {
    return;
  }

  if (!systemInited) {
    initTime = syncCloud2->header.stamp.toSec();
    systemInited = true;
  }

  double time = syncCloud2->header.stamp.toSec();
  double timeLasted = time - initTime;

  syncCloudInd = (syncCloudInd + 1) % keepSyncCloudNum;
  syncCloudTime[syncCloudInd] = time;

  tempCloud3->clear();
  pcl::PointXYZI point;
  double halfDSRate = cloudDSRate / 2.0 - 0.5;
  const float* syncCloud2Pointer = reinterpret_cast<const float*>(&syncCloud2->data[0]);
  for (double i = halfDSRate; i < imageHeight; i += cloudDSRate) {
    for (double j = halfDSRate; j < imageWidth; j += cloudDSRate) {
      int pixelCount = 0;
      float val, valSum = 0;
      int is = int(i - halfDSRate), ie = int(i + halfDSRate);
      int js = int(j - halfDSRate), je = int(j + halfDSRate);
      for (int ii = is; ii <= ie; ii++) {
        for (int jj = js; jj <= je; jj++) {
          val = syncCloud2Pointer[ii * imageWidth + jj];
          if (val > 0.3 && val < 7) {
            valSum += val;
            pixelCount++;
          }
        }
      }

      if (pixelCount > 0) {
        double ud = (kDepth[2] - j) / kDepth[0];
        double vd = (kDepth[5] - i) / kDepth[4];

        val = valSum / pixelCount;

        point.z = val;
        point.x = ud * val;
        point.y = vd * val;
        point.intensity = timeLasted;

        tempCloud3->push_back(point);
      }
    }
  }

  pcl::PointCloud<pcl::PointXYZI>::Ptr syncCloudPointer = syncCloudArray[syncCloudInd];
  syncCloudPointer->clear();

  pcl::VoxelGrid<pcl::PointXYZI> downSizeFilter;
  downSizeFilter.setInputCloud(tempCloud3);
  downSizeFilter.setLeafSize(0.1, 0.1, 0.1);
  downSizeFilter.filter(*syncCloudPointer);
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "processDepthmap");
  ros::NodeHandle nh;

  for (int i = 0; i < keepSyncCloudNum; i++) {
    pcl::PointCloud<pcl::PointXYZI>::Ptr syncCloudTemp(new pcl::PointCloud<pcl::PointXYZI>());
    syncCloudArray[i] = syncCloudTemp;
  }

  ros::Subscriber voDataSub = nh.subscribe<nav_msgs::Odometry> ("cam_to_init", 5, voDataHandler);

  ros::Subscriber syncCloudSub = nh.subscribe<sensor_msgs::Image>
                                 ("/camera/depth_registered/image", 1, syncCloudHandler);

  ros::Publisher depthCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("depth_cloud", 5);
  depthCloudPubPointer = &depthCloudPub;

  ros::spin();

  return 0;
}
