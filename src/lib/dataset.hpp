#ifndef _CLIF_DATASET_H
#define _CLIF_DATASET_H

#include "attribute.hpp"
#include "datastore.hpp"

namespace clif {
  
class Datastore;

class Intrinsics {
  public:
    Intrinsics() {};
    Intrinsics(Attributes *attrs, boost::filesystem::path &path) { load(attrs, path); };
    void load(Attributes *attrs, boost::filesystem::path path);
    cv::Mat* getUndistMap(double depth, int w, int h);
    
    double f[2], c[2];
    DistModel model = DistModel::INVALID;
    std::vector<double> cv_dist;
    cv::Mat cv_cam;

  private:
    cv::Mat _undist_map;
};

/** Class representing a single light field dataset.
 * This class inherits from Attributes for Metadata handling and from Datastore for actual image/light field IO.
 * The class mostly wraps functionality from the inherited classes, while higher level light field handling like EPI generation is available in the respective library bindings (TODO ref and document opencv/vigra interfaces)
 * Not that functions taking a path (either as std::string or boost::filesystem::path) all reference the path relative to the dataset root. An attribute adressed as \a /blub/someattribute is actually stored under \a /clif/datasetname/blub/someattribute.
 */
class Dataset : public Attributes, public Datastore {
  public:
    Dataset() {};
    //FIXME use only open/create methods?
    //Dataset(H5::H5File &f_, std::string path);
    
    /** Open the dataset \a name from file \a f_ */
    void open(H5::H5File &f_, std::string name);
    
    //link other into this file, attributes are copied, "main" datastore is linked read-only
    //TODO link other existing datastores!
    void link(H5::H5File &f_, const Dataset *other);
    
    /** Create or open (if existing) the dataset \a name in file \a f_ */
    void create(H5::H5File &f_, std::string name);
      
    //writes only Attributes! FIXME hide Attributes::Write
    //TODO automatically call this on file close (destructor)
    /** Sync attributes back to the underlying HDF5 file 
     */
    void writeAttributes() { Attributes::write(f, _path); }
    
    /** Get the calibration Datastore - use methods of Datastore to access calibration images 
     */
    Datastore *getCalibStore();
    /** Create new or open (if existing) calibration Datastore 
     */
    Datastore *createCalibStore();
    
    //FIXME fix or remove this method
    bool valid();
    
    /** load the intrinsics group with name \a intrset.
     * default (empty string) will load the first intrinsics group from the file, 
     * which is also the group that is loaded on opening the dataset. The specified
     * intrinsics group will be used for automatic processing like undistortion.
     */
    void load_intrinsics(std::string intrset = std::string());
    
    /** Create path from \a parent path and \a child name.
     * the default empty string for \a child will create a path to the first child found under parent
     */
    boost::filesystem::path subGroupPath(boost::filesystem::path parent, std::string child = std::string());
    
    /** return the actual full path of the dataset root in the HDF5 file
     */
    boost::filesystem::path path();
    
    
    /** The internal HDF5 reference
     */
    H5::H5File f;

    //TODO if attributes get changed automatically refresh intrinsics on getIntrinsics?
    //TODO hide and create accessor
    Intrinsics intrinsics;
private:
    Datastore *calib_images = NULL;
    std::string _path;
    
    //hide copy assignment operator
    Dataset& operator=(const Dataset& other);
};
  
}

#endif