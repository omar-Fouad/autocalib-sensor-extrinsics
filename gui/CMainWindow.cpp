#include <CMainWindow.h>
#include <ui_CMainWindow.h>
#include <observation_tree/CObservationTreeModel.h>
#include <config/CPlaneMatchingConfig.h>
#include <config/CLineMatchingConfig.h>
#include <calib_solvers/CPlaneMatching.h>

#include <mrpt/obs/CObservation3DRangeScan.h>
#include <mrpt/maps/PCL_adapters.h>
//#include <mrpt/maps/CSimplePointsMap.h>
#include <mrpt/maps/CColouredPointsMap.h>
#include <pcl/search/impl/search.hpp>

#include <QFileDialog>
#include <QSpinBox>
#include <QDebug>

using namespace mrpt::obs;

CMainWindow::CMainWindow(QWidget *parent) :
	QMainWindow(parent),
	m_model(nullptr),
    m_sync_model(nullptr),
	m_ui(new Ui::CMainWindow)
{
	m_ui->setupUi(this);

	connect(m_ui->load_rlog_button, SIGNAL(clicked(bool)), this, SLOT(loadRawlog()));
	connect(m_ui->algo_cbox, SIGNAL(currentIndexChanged(int)), this, SLOT(algosIndexChanged(int)));
	connect(m_ui->observations_treeview, SIGNAL(clicked(QModelIndex)), this, SLOT(itemClicked(QModelIndex)));
	connect(m_ui->sync_observations_button, SIGNAL(clicked(bool)), this, SLOT(syncObservations()));

	connect(m_ui->irx_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));
	connect(m_ui->iry_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));
	connect(m_ui->irz_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));
	connect(m_ui->itx_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));
	connect(m_ui->ity_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));
	connect(m_ui->itz_sbox, SIGNAL(valueChanged(double)), this, SLOT(initCalibChanged(double)));

	setWindowTitle("Automatic Calibration of Sensor Extrinsics");
	m_calib_started = false;
	m_ui->viewer_container->updateText("Welcome to autocalib-sensor-extrinsics!");
	m_ui->viewer_container->updateText("Set your initial (rough) calibration values and load your rawlog file to get started.");
	m_recent_file = m_settings.value("recent").toString();

	m_init_calib.fill(0);
}

CMainWindow::~CMainWindow()
{
	m_settings.setValue("recent", m_recent_file);
	delete m_ui;
}

void CMainWindow::algosIndexChanged(int index)
{
	switch(index)
	{
	case 0:
	{
		if(m_config_widget)
			m_config_widget.reset();
		break;
	}

	case 1:
	{
		m_config_widget = std::make_shared<CPlaneMatchingConfig>();
		qobject_cast<QVBoxLayout*>(m_ui->config_dockwidget_contents->layout())->insertWidget(4, m_config_widget.get());
		break;
	}

	case 2:
	{
		m_config_widget = std::make_shared<CLineMatchingConfig>();
		qobject_cast<QVBoxLayout*>(m_ui->config_dockwidget_contents->layout())->insertWidget(4, m_config_widget.get());
		break;
	}
	}
}

void CMainWindow::loadRawlog()
{
	QString path;

	if(!m_recent_file.isEmpty())
	{
		QFileInfo fi(m_recent_file);
		path = fi.absolutePath();
	}

	QString file_name = QFileDialog::getOpenFileName(this, tr("Open File"), path, tr("Rawlog Files (*.rawlog *.rawlog.gz)"));

	if(file_name.isEmpty())
		return;

	m_ui->status_bar->showMessage("Loading Rawlog...");
	m_ui->observations_treeview->setDisabled(true);
	m_ui->observations_description_textbrowser->setDisabled(true);
	m_ui->observations_delay->setDisabled(true);
	m_ui->sensors_selection_list->setDisabled(true);
	m_ui->sync_observations_button->setDisabled(true);
	m_recent_file = file_name;

	if(m_model)
		delete m_model;

	m_model = new CObservationTreeModel(m_ui->observations_treeview);
	m_model->addTextObserver(m_ui->viewer_container);
	m_model->loadModel(file_name.toStdString());

	if((m_model->getRootItem()) != nullptr)
	{
		m_ui->observations_treeview->setDisabled(false);
		m_ui->observations_treeview->setModel(m_model);
		m_ui->status_bar->showMessage("Rawlog loaded!");
		m_ui->viewer_container->updateText("Select the calibration algorithm to continue.");
		m_ui->observations_description_textbrowser->setDisabled(false);
		m_ui->sensors_selection_list->setDisabled(false);
		m_ui->observations_delay->setDisabled(false);
		m_ui->sync_observations_button->setDisabled(false);

		QStringList sensor_labels = m_model->getSensorLabels();

		for(size_t i = 0; i < sensor_labels.size(); i++)
		{
			QListWidgetItem *item = new QListWidgetItem;
			item->setText(sensor_labels[i]);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(Qt::Checked);
			m_ui->sensors_selection_list->insertItem(i, item);
		}
	}

	else
	{
		m_ui->status_bar->showMessage("Loading aborted!");
		m_ui->viewer_container->updateText("Loading was aborted. Try again.");
	}
}

void CMainWindow::itemClicked(const QModelIndex &index)
{
	if(index.isValid())
	{
		CObservationTreeItem *item = static_cast<CObservationTreeItem*>(index.internalPointer());

		std::stringstream update_stream;
		std::string viewer_text;
		int viewer_id, sensor_id;

		CObservation3DRangeScan::Ptr obs_item;
        mrpt::maps::CPointsMap::Ptr map;

        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGBA>);
		mrpt::img::CImage image;

		obs_item = std::dynamic_pointer_cast<CObservation3DRangeScan>(m_model->observationData(index));
		obs_item->getDescriptionAsText(update_stream);

		map = mrpt::make_aligned_shared<mrpt::maps::CColouredPointsMap>();
//            map->colorScheme.scheme = mrpt::maps::CColouredPointsMap::cmFromIntensityImage;

		map->insertObservation(obs_item.get());
		map->getPCLPointCloud(*cloud);
//            map->getPCLPointCloudXYZRGB(*cloud);
		std::cout << cloud->size() << " " << cloud->height << "x" << cloud->width << "\n";

			//image = cv::cvarrToMat(obs_item->intensityImage.getAs<IplImage>());
		image = obs_item->intensityImage;

		sensor_id = m_model->getObsLabels().indexOf(QString::fromStdString(obs_item->sensorLabel + " : " + obs_item->GetRuntimeClass()->className)) + 1;
		viewer_id = sensor_id;

		viewer_text = (m_model->data(index)).toString().toStdString();

		m_ui->viewer_container->updateViewer(viewer_id, cloud, viewer_text);
		m_ui->viewer_container->updateImageViewer(viewer_id, image);

		if(m_calib_started)
			m_plane_matching->publishPlaneCloud(item->parentItem()->row(), item->row(), sensor_id);

		m_ui->observations_description_textbrowser->setText(QString::fromStdString(update_stream.str()));
	}
}

void CMainWindow::initCalibChanged(double value)
{
	QSpinBox *sbox = (QSpinBox*)sender();

	if(sbox->accessibleName() == QString("irx"))
		m_init_calib[0] = value;
	else if(sbox->accessibleName() == QString("iry"))
		m_init_calib[1] = value;
	else if(sbox->accessibleName() == QString("irz"))
		m_init_calib[2] = value;
	else if(sbox->accessibleName() == QString("itx"))
		m_init_calib[3] = value;
	else if(sbox->accessibleName() == QString("ity"))
		m_init_calib[4] = value;
	else if(sbox->accessibleName() == QString("itz"))
		m_init_calib[5] = value;
}

void CMainWindow::syncObservations()
{
	QStringList selected_sensor_labels;
	QListWidgetItem *item;

	m_ui->grouped_observations_treeview->setDisabled(true);

	for(size_t i = 0; i < m_ui->sensors_selection_list->count(); i++)
	{
		item = m_ui->sensors_selection_list->item(i);
		if(item->checkState() == Qt::Checked)
			selected_sensor_labels.push_back(item->text());
	}

	if(!(selected_sensor_labels.size() > 1))
		m_sync_model = nullptr;

	else
	{
		if(m_sync_model)
		{
			delete m_sync_model;
		}

		// creating a copy of the model
		m_sync_model = new CObservationTreeModel(m_ui->grouped_observations_treeview);
		m_sync_model->setRootItem(new CObservationTreeItem(QString("root")));

		for(size_t i = 0; i < m_model->getRootItem()->childCount(); i++)
		{
			m_sync_model->getRootItem()->appendChild(m_model->getRootItem()->child(i));
		}

		m_sync_model->syncObservations(selected_sensor_labels, m_ui->observations_delay->value());
		m_ui->grouped_observations_treeview->setDisabled(false);
		m_ui->grouped_observations_treeview->setModel(m_sync_model);
	}
}

void CMainWindow::runPlaneMatchingCalib(TPlaneMatchingParams params /* to be replaced by a generic calib parameters object */)
{
	if(m_sync_model != nullptr)
	{
		m_plane_matching = new CPlaneMatching(m_sync_model, m_init_calib, params);
		m_plane_matching->addTextObserver(m_ui->viewer_container);
		m_plane_matching->addPlanesObserver(m_ui->viewer_container);
		m_plane_matching->extractPlanes();
		m_calib_started = true;
	}

	else
		m_ui->viewer_container->updateText("Error. Choose atleast two sensors!");
}

void CMainWindow::runLineMatchingCalib()
{
	if(m_sync_model != nullptr)
	{
		m_line_matching = new CLineMatching(m_sync_model);
		//m_line_matching->extractLines();
		m_calib_started = true;
	}

	else
		m_ui->viewer_container->updateText("Error. Choose atleast two sensors!");
}
