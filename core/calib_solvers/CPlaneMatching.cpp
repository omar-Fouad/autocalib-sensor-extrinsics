#include <calib_solvers/CPlaneMatching.h>

#include <mrpt/obs/CObservation3DRangeScan.h>
#include <mrpt/math/types_math.h>
#include <mrpt/maps/PCL_adapters.h>

#include <pcl/search/impl/search.hpp>
#include <pcl/segmentation/organized_multi_plane_segmentation.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/features/normal_3d.h>
#include <pcl/features/integral_image_normal.h>
#include <pcl/common/time.h>

using namespace mrpt::obs;

CPlaneMatching::CPlaneMatching(CObservationTreeModel *model, std::array<double, 6> init_calib, TPlaneMatchingParams params)
{
	m_model = model;
	m_init_calib = init_calib;
	m_params = params;
}

CPlaneMatching::~CPlaneMatching()
{
}

void CPlaneMatching::addObserver(CObserver *observer)
{
	m_observers.push_back(observer);
}

void CPlaneMatching::publishEvent(const std::string &msg)
{
	for(CObserver *observer : m_observers)
	{
		observer->onEvent(msg);
	}
}

void CPlaneMatching::publishCloud(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &cloud)
{
	for(CObserver *observer : m_observers)
	{
		observer->onCloud(cloud);
	}
}

void CPlaneMatching::run()
{
	// For running all steps at a time
}

std::vector<pcl::PointCloud<pcl::PointXYZRGBA>::Ptr> CPlaneMatching::extractPlanes()
{
	publishEvent("****Running plane matching calibration algorithm****");

	CObservationTreeItem *root_item, *tree_item;
	CObservation3DRangeScan::Ptr obs_item;
	root_item = m_model->getRootItem();

	pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGBA>());

	T3DPointsProjectionParams params;
	params.MAKE_DENSE = false;
	params.MAKE_ORGANIZED = true;

// Testing with just the first observation

	tree_item = root_item->child(10);
	obs_item = std::dynamic_pointer_cast<CObservation3DRangeScan>(tree_item->child(0)->getObservation());
	obs_item->project3DPointsFromDepthImageInto(*cloud, params);

	runSegmentation(cloud);

// For all observations

//	for(int i = 0; i < root_item->childCount(); i++)
//	{
//		tree_item = root_item->child(i);
//		for(int j = 0; j < tree_item->childCount(); j++)
//		{
//			publishEvent("**Extracting planes from #" + std::to_string(j) + " observation in observation set #" + std::to_string(i) + "**");

//			obs_item = std::dynamic_pointer_cast<CObservation3DRangeScan>(tree_item->child(j)->getObservation());
//			obs_item->project3DPointsFromDepthImageInto(*cloud, params);
//			runSegmentation(cloud);
//		}
//	}

	return m_extracted_plane_clouds;
}

void CPlaneMatching::runSegmentation(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &cloud)
{
	double plane_extract_start = pcl::getTime();
	unsigned min_inliers = m_params.min_inliers_frac * cloud->size();

	pcl::IntegralImageNormalEstimation<pcl::PointXYZRGBA, pcl::Normal> normal_estimation;

	if(m_params.normal_estimation_method == 0)
		normal_estimation.setNormalEstimationMethod(normal_estimation.COVARIANCE_MATRIX);
	else if(m_params.normal_estimation_method == 1)
		normal_estimation.setNormalEstimationMethod(normal_estimation.AVERAGE_3D_GRADIENT);
	else
		normal_estimation.setNormalEstimationMethod(normal_estimation.AVERAGE_DEPTH_CHANGE);

	normal_estimation.setDepthDependentSmoothing(m_params.depth_dependent_smoothing);
	normal_estimation.setMaxDepthChangeFactor(m_params.max_depth_change_factor);
	normal_estimation.setNormalSmoothingSize(m_params.normal_smoothing_size);

	pcl::PointCloud<pcl::Normal>::Ptr normal_cloud(new pcl::PointCloud<pcl::Normal>);
	normal_estimation.setInputCloud(cloud);
	normal_estimation.compute(*normal_cloud);

	pcl::OrganizedMultiPlaneSegmentation<pcl::PointXYZRGBA, pcl::Normal, pcl::Label> multi_plane_segmentation;
	multi_plane_segmentation.setMinInliers(min_inliers);
	multi_plane_segmentation.setAngularThreshold(m_params.angle_threshold);
	multi_plane_segmentation.setDistanceThreshold(m_params.dist_threshold);
	multi_plane_segmentation.setInputNormals(normal_cloud);
	multi_plane_segmentation.setInputCloud(cloud);

	std::vector<pcl::PlanarRegion<pcl::PointXYZRGBA>, Eigen::aligned_allocator<pcl::PlanarRegion<pcl::PointXYZRGBA>>> regions;
	std::vector<pcl::ModelCoefficients> model_coefficients;
	std::vector<pcl::PointIndices> inlier_indices;
	pcl::PointCloud<pcl::Label>::Ptr labels(new pcl::PointCloud<pcl::Label>);
	std::vector<pcl::PointIndices> label_indices;
	std::vector<pcl::PointIndices> boundary_indices;

	multi_plane_segmentation.segmentAndRefine(regions, model_coefficients, inlier_indices, labels, label_indices, boundary_indices);
	double plane_extract_end = pcl::getTime();

	std::stringstream stream;
	stream << inlier_indices.size() << " plane(s) detected\n" << "Time elapsed: " << double(plane_extract_end - plane_extract_start) << std::endl;

	publishEvent(stream.str());

	pcl::PointCloud<pcl::PointXYZRGBA>::Ptr segmented_cloud(new pcl::PointCloud<pcl::PointXYZRGBA>);

	for(size_t i = 0; i < inlier_indices.size(); i++)
	{
		std::vector<int> indices = inlier_indices[i].indices;
		for(size_t j = 0; j < indices.size(); j++)
		{
			segmented_cloud->points.push_back(cloud->points[indices[j]]);
		}
	}

	publishCloud(segmented_cloud);
	m_extracted_plane_clouds.push_back(segmented_cloud);
}

void CPlaneMatching::proceed()
{
}
