#include "dataset.hpp"

#include <opencv2/imgproc/imgproc.hpp>

#include "hdf5.hpp"

namespace clif {
void Intrinsics::load(Attributes *attrs, boost::filesystem::path path)
{
  Attribute *a = attrs->get(path / "type");
  
  _undist_map = cv::Mat();
  
  if (!a) {
    printf("no valid intrinsic model! %s\n", path.generic_string().c_str());
    model = DistModel::INVALID;
    return;
  }
  
  a->readEnum(model);
  
  f[0] = 0;
  f[1] = 0;
  c[0] = 0;
  c[1] = 0;
  cv_cam = cv::Mat::eye(3,3,CV_64F);
  cv_dist.resize(0);
  
  attrs->get(path / "projection", f, 2);
  cv_cam.at<double>(0,0) = f[0];
  cv_cam.at<double>(1,1) = f[1];
  
  a = attrs->get(path / "projection_center");
  if (a) {
    a->get(c, 2);
    cv_cam.at<double>(0,2) = c[0];
    cv_cam.at<double>(1,2) = c[1];
  }
  
  if (model == DistModel::CV8)
    attrs->get(path / "opencv_distortion", cv_dist);
}

cv::Mat* Intrinsics::getUndistMap(double depth, int w, int h)
{
  if (_undist_map.total())
    return &_undist_map;
  
  cv::Mat tmp;
  
  cv::initUndistortRectifyMap(cv_cam, cv_dist, cv::noArray(), cv::noArray(), cv::Size(w, h), CV_32FC2, _undist_map, tmp);
  
  return &_undist_map;
}

//FIXME f could point to core driver (memory only) file!
void Dataset::open(H5::H5File &f_, std::string name)
{
  _path = std::string("/clif/").append(name);
  _memory_file = false;
  f = f_;
    
  //static_cast<clif::Dataset&>(*this) = clif::Dataset(f, fullpath);
  
  if (h5_obj_exists(f, _path.c_str())) {
    //static_cast<Attributes&>(*this) = Attributes(f, _path);
    Attributes::open(f, _path);
    
    //FIXME specificy which one!?
    load_intrinsics();
  }
  
  if (!Dataset::valid()) {
    printf("could not open dataset %s\n", _path.c_str());
    return;
  }
  
  Datastore::open(this, "data");
}

//TODO use priority!
boost::filesystem::path Dataset::subGroupPath(boost::filesystem::path parent, std::string child)
{
  std::vector<std::string> list;
  
  if (child.size())
    return parent / child;

  listSubGroups(parent, list);
  assert(list.size());
  
  return parent / list[0];
}

Datastore *Dataset::createCalibStore()
{
  if (calib_images)
    return calib_images;
  
  getCalibStore();
  
  if (calib_images)
    return calib_images;
    
  calib_images = new clif::Datastore();
  calib_images->create("calibration/images/data", this);
  return calib_images;
}

//return pointer to the calib image datastore - may be manipulated
Datastore *Dataset::getCalibStore()
{
  boost::filesystem::path dataset_path;
  dataset_path = path() / "calibration/images/data";
  
  std::cout << dataset_path << clif::h5_obj_exists(f, dataset_path) << calib_images << std::endl;
  
  if (!calib_images && clif::h5_obj_exists(f, dataset_path)) {
    calib_images = new clif::Datastore();
    calib_images->open(this, "calibration/images/data");
  }
  
  return calib_images;
}

void Dataset::create(H5::H5File &f_, std::string name)
{
  _path = std::string("/clif/").append(name);
  f = f_;
  
  //TODO check if already exists and fail if it does?
  
  Datastore::create("data", this);
}

//link second dataset into the place of current dataset
void Dataset::link(const Dataset *other)
{  
  assert(f.getId() != H5I_INVALID_HID);
  
  attrs = other->attrs;
  
  _path = other->_path;
  
  intrinsics = other->intrinsics;
  calib_images = NULL;
  
  Datastore::link(other, this);
 
  //FIXME set stores to 0!
  
  //iterate stores...
  for(auto iter : other->_stores) {
    addStore(iter.first);
    _stores[iter.first]->link(iter.second, this);
  }
}

//link second dataset into the place of current dataset
void Dataset::link(H5::H5File &f_, const Dataset *other)
{
  f = f_;
  
  link(other);
}

bool Dataset::memoryFile()
{
  return _memory_file;
}

//link second dataset into the place of current dataset
void Dataset::memory_link(const Dataset *other)
{ 
  _memory_file = true;
  f = h5_memory_file();
  link(other);
}

boost::filesystem::path Dataset::path()
{
  return boost::filesystem::path(_path);
}

//FIXME
bool Dataset::valid()
{
  return true;
}

void Dataset::load_intrinsics(std::string intrset)
{
  std::vector<std::string> sets;
  
  if (!intrset.size()) {
    listSubGroups("calibration/intrinsics", sets);
    if (!sets.size())
      return;
    //TODO select with priority?!
    intrset = sets[0];
  }
  
  intrinsics.load(this, boost::filesystem::path() / "calibration/intrinsics" / intrset);
}

Datastore *Dataset::getStore(const std::string &path)
{
  auto it_find = _stores.find(path);
  
  if (it_find == _stores.end())
    return NULL;
  else
    return it_find->second;
}

Datastore *Dataset::addStore(const std::string &path)
{
  Datastore *store = new Datastore;
  store->create(path, this);
  //FIXME delete previous store!
  assert(store);
  _stores[store->getDatastorePath()] = store;
  
  return store;
}

/*
Dataset& Dataset::operator=(const Dataset& other)
{
  if (this == &other)
    return *this;
  
  this->Attributes::reset();
  
  assert(f.getId() != H5I_INVALID_HID);
  
  //use this file!
  H5::H5File f;

  //TODO reset intrinsics
  
  calib_images = NULL;
  
  _path = other._path;;
  
  return *this;
}*/

} //namespace clif
