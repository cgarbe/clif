#include "clif_vigra.hpp"

#include <cstddef>

#include <sstream>
#include <string>

#include "clif_cv.hpp"

#include "opencv2/highgui/highgui.hpp"

namespace clif {
  
using namespace vigra;

  
Shape2 imgShape(Datastore *store)
{
  cv::Size size = imgSize(store);
  
  return Shape2(size.width, size.height);
}
  
template<typename T> class readimage_dispatcher {
public:
  void operator()(Datastore *store, uint idx, void **raw_channels, int flags = 0, float scale = 1.0)
  {
    //cast
    std::vector<MultiArrayView<2, T>> **channels = reinterpret_cast<std::vector<MultiArrayView<2, T>> **>(raw_channels);
    
    //allocate correct type
    if (*channels == NULL)
      *channels = new std::vector<vigra::MultiArrayView<2, T>>(store->channels());
    
    //read
    std::vector<cv::Mat> cv_channels;
    readCvMat(store, idx, cv_channels, flags, scale);
    
    //store in multiarrayview
    for(int c=0;c<(*channels)->size();c++)
      (**channels)[c] = vigra::MultiArrayView<2, T>(imgShape(store), (T*)cv_channels[c].data);
  }
};

void readImage(Datastore *store, uint idx, void **channels, int flags, float scale)
{
  store->call<readimage_dispatcher>(store, idx, channels, flags, scale);
}

void readImage(Datastore *store, uint idx, FlexMAV<2> &channels, int flags, float scale)
{
  std::vector<cv::Mat> cv_channels;
  readCvMat(store, idx, cv_channels, flags, scale);
  
  channels.create(imgShape(store), store->type(), cv_channels);
}

template<typename T> class readepi_dispatcher {
public:
  void operator()(clif::Subset3d *subset, void **raw_channels, int line, double disparity, ClifUnit unit = ClifUnit::PIXELS, int flags = 0, Interpolation interp = Interpolation::LINEAR, float scale = 1.0)
  {
    //cast
    std::vector<MultiArray<2, T>> **channels = reinterpret_cast<std::vector<MultiArray<2, T>> **>(raw_channels);
    
    //allocate correct type
    if (*channels == NULL)
      *channels = new std::vector<vigra::MultiArray<2, T>>(subset->getDataset()->channels());
    
    //read
    std::vector<cv::Mat> cv_channels;
    subset->readEPI(cv_channels, line, disparity, unit, flags, interp, scale);
    
    Shape2 shape(cv_channels[0].size().width, cv_channels[0].size().height);
    
    //store in multiarrayview
    for(int c=0;c<(*channels)->size();c++) {
      //TODO implement somw form of zero copy...
      (**channels)[c].reshape(shape);
      (**channels)[c] = MultiArrayView<2, T>(shape, (T*)cv_channels[c].data);
    }
  }
};

void readEPI(clif::Subset3d *subset, void **channels, int line, double disparity, ClifUnit unit, int flags, Interpolation interp, float scale)
{
  subset->getDataset()->call<readepi_dispatcher>(subset, channels, line, disparity, unit, flags, interp, scale);
}

}