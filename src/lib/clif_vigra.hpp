#ifndef _CLIF_VIGRA_H
#define _CLIF_VIGRA_H

#include "clif.hpp"
#include "subset3d.hpp"

#include <vigra/multi_array.hxx>

namespace clif {
    
  vigra::Shape2 imgShape(Datastore *store);
  
  void readImage(Datastore *store, uint idx, void **channels, int flags = 0, float scale = 1.0);
  
  void readEPI(Subset3d *subset, void **channels, int line, double disparity, ClifUnit unit = ClifUnit::PIXELS, int flags = 0, Interpolation interp = Interpolation::LINEAR, float scale = 1.0);
  
  //void readSubset3d(Datastore *store, uint idx, void **volume, int flags = 0, float scale = 1.0);
  
}

#endif