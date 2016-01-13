#include "cam.hpp"

#include "dataset.hpp"

#ifdef CLIF_WITH_UCALIB
  #include "ucalib/gencam.hpp"
#endif
  
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/calib3d/calib3d.hpp>

namespace clif {

DepthDist::DepthDist(const cpath& path, double at_depth)
  : Tree_Derived_Base<DepthDist>(path), _depth(at_depth)
{
  
}

static void _genmap(Mat_<cv::Point2f> & _maps, double _depth, const Idx & map_pos, Mat_<float> &corr_line_m, Mat_<float> &extrinsics_m, int *imgsize)
{
  std::cout << "generate map for " << map_pos << std::endl;
  
  std::vector<cv::Vec4d> linefits(corr_line_m["x"]*corr_line_m["y"]);
  
  Idx lines_pos({IR(4, "line"), map_pos.r("x","views")});
  
  for(int j=0;j<corr_line_m["y"];j++)
    for(int i=0;i<corr_line_m["x"];i++)
      for (int e=0;e<4;e++) {
        lines_pos[0] = e;
        lines_pos["x"] = i;
        lines_pos["y"] = j;
        linefits[j*corr_line_m["x"]+i][e] = corr_line_m(lines_pos);
      }
      
      printf("create gencam\n");
    GenCam cam(linefits, cv::Point2i(imgsize[0],imgsize[1]), cv::Point2i(corr_line_m[1],corr_line_m[2]));
  
  
  /*Idx extr_size(extrinsics_m);
  
  cam.extrinsics.resize(6*extrinsics_m["views"]);
  
  for(auto extr_pos : Idx_It_Dim(extrinsics_m, "views"))
    for(int i=0;i<6;i++) {
      extr_pos["data"] = i;
      extr_pos["channels"] = map_pos["channels"];
      extr_pos["cams"] = map_pos["cams"];
      cam.extrinsics[6*extr_pos["views"]+i] = extrinsics_m(extr_pos);
      std::cout << "extr:" << extr_pos << ": " << extrinsics_m(extr_pos) << std::endl;
    }*/
    
    
    
  double d = _depth;
  if (isnan(d) || d < 0.1)
    d = 1000000000000.0;
  
  printf("depth: %f\n", d);
  
  cv::Mat map = cvMat(_maps.bind(3, map_pos["cams"]).bind(2, map_pos["channels"]));
  cam.get_undist_map_for_depth(map, d);
}
  
bool DepthDist::load(Dataset *set)
{
  try {
    Idx proxy_size;
    int imgsize[2];
    
    set->getEnum(path()/"type", _type);
        
    if (_type != DistModel::UCALIB)
      return true;
    
    
#ifndef CLIF_WITH_UCALIB
    return true;
#else
      
    //FIXME cams might be missing!
    Mat_<float> corr_line_m = set->readStore(path()/"lines");
    corr_line_m.names({"line","x","y","channels","cams"});
    
    Mat_<float> extrinsics_m;
    
    set->get(path()/"extrinsics", extrinsics_m);
    extrinsics_m.names({"data","channels","cams","views"});
    set->get(path()/"source/source/img_size", imgsize, 2);
    
    _maps.create({IR(imgsize[0], "x"),IR(imgsize[1], "y"), extrinsics_m.r("channels","cams")});
    
    float extrinsics_main[6];
    for(int i=0;i<6;i++)
      extrinsics_main[i] = extrinsics_m(Idx({i,extrinsics_m["channels"]/2,extrinsics_m["cams"]/2,0}));
    
    
    //center rotation and translation
    cv::Matx31f c_r(extrinsics_main[0],extrinsics_main[1],extrinsics_main[2]);
    cv::Matx31f c_t(extrinsics_main[3],extrinsics_main[4],extrinsics_main[5]);
    cv::Matx33f c_r_m;
    cv::Rodrigues(c_r, c_r_m);
    
    for(auto pos : Idx_It_Dims(extrinsics_m, "channels","cams")) {
      //to move lines from local to ref view do:
      //undo local translation
      //undo local rotation (we are now in target coordinates)
      //do ref rotation
      //do ref translation
      cv::Matx31f l_r(extrinsics_m({0,pos.r("channels","views")}),
                      extrinsics_m({1,pos.r("channels","views")}),
                      extrinsics_m({2,pos.r("channels","views")}));
      cv::Matx31f l_t(extrinsics_m({3,pos.r("channels","views")}),
                      extrinsics_m({4,pos.r("channels","views")}),
                      extrinsics_m({5,pos.r("channels","views")}));
      cv::Matx33f l_r_m, l_r_m_t;
      cv::Rodrigues(l_r, l_r_m);
      transpose(l_r_m, l_r_m_t);
      
      
      if (pos["channels"] == extrinsics_m["channels"]/2
        && pos["cams"] == extrinsics_m["cams"]/2)
        printf("shoud stay the same!                                       !!!!!!!!!!!!!!!!!!!!!\n");
      
      std::cout << pos << std::endl;
      
      
      cv::Matx33f rotm = c_r_m*l_r_m_t;

      
      for(auto pos_lines : Idx_It_Dims(corr_line_m, "x", "y")) {
        cv::Matx31f offset(corr_line_m({0, pos_lines.r("x","y"), pos.r("channels","cams")}),
                           corr_line_m({1, pos_lines.r("x","y"), pos.r("channels","cams")}),
                           0.0);
        cv::Matx31f dir(corr_line_m({2, pos_lines.r("x","y"), pos.r("channels","cams")}),
                           corr_line_m({3, pos_lines.r("x","y"), pos.r("channels","cams")}),
                           1.0);
        
        
        if (pos_lines["x"] == 32 && pos_lines["y"] == 24) {
          std::cout << "start: " << offset << "\n" << dir << std::endl;
          
          
          std::cout << "rot1: " << l_r_m_t << std::endl;
          std::cout << "rot2: " << c_r_m << std::endl;
          std::cout << "rot1*rot2: " << c_r_m*l_r_m_t << std::endl;
        }
        
        offset -= l_t;
        offset = rotm * offset;
        //offset = l_r_m_t * offset;
        //offset = c_r_m * offset;
        offset += c_t;
        
        //dir -= l_t;
        dir = rotm*dir;
        //dir = l_r_m_t * offset;
        //dir = c_r_m * offset;
        //dir += c_t;
        
        //normalize
        dir *= 1.0/dir(2);
        offset -= offset(2)*dir;
        
        if (pos_lines["x"] == 32 && pos_lines["y"] == 24)
          std::cout << "res: " << offset << dir << std::endl;
        
        corr_line_m({0, pos_lines.r("x","y"), pos.r("channels","cams")}) = offset(0);
        corr_line_m({1, pos_lines.r("x","y"), pos.r("channels","cams")}) = offset(1);
        
        corr_line_m({2, pos_lines.r("x","y"), pos.r("channels","cams")}) = dir(0);
        corr_line_m({3, pos_lines.r("x","y"), pos.r("channels","cams")}) = dir(1);
      }
      
    }
    
    std::cout << "maps:" << _maps << std::endl;
    
    int cam_count = corr_line_m.dim("cams");
    if (cam_count == -1)
      cam_count = 1;
    
    int channels = corr_line_m["channels"];
        
    std::vector<cv::Point3f> ref_points;
    
    /*_genmap(_maps, _depth, Idx({IR(0,"line"),IR(0,"x"),IR(0,"y"),IR(corr_line_m["channels"]/2,"channels"),IR(corr_line_m["cams"]/2,"cams")}), corr_line_m, extrinsics_m, imgsize);*/
    
    //_ref_points = ref_points;
    
    for(auto map_pos : Idx_It_Dims(corr_line_m, "channels","cams")) {
      _genmap(_maps, _depth, map_pos, corr_line_m, extrinsics_m, imgsize);
    }
    
    return true;
#endif
  }
  catch (std::invalid_argument) {
    return false;
  }
}

void DepthDist::undistort(const clif::Mat & src, clif::Mat & dst, const Idx & pos, int interp)
{
  dst.create(src.type(), src);
  
  if (interp == -1)
    interp = cv::INTER_LINEAR;
  
  if (!_maps.size())
    dst = src;
    
  Mat map_pos(_maps);
  
  Idx pos_named = pos;
  
  //FIXME
  if (pos_named.size() == 5)
    pos_named.names({"x","y","channels","cams","views"});
  else if (pos_named.size() == 4)
    pos_named.names({"x","y","channels","views"});
  else
    abort();
  
  Mat maps_cam;
  
  if (pos_named.dim("cams") != -1)
    maps_cam = _maps.bind(3, pos_named["cams"]);
  else
    maps_cam = _maps;
  
  if (_maps.size()) {
    for(int c=0;c<src[2];c++) {
      
      
      
      cv::Mat src_dup = cvMat(src.bind(2, c)).clone();
      /*
      for(int i=0;i<4;i++) {
        cv::Point2f ref = _ref_point_proj(i, pos_named["channels"], pos_named["cams"]);
        circle(src_dup, ref*32, 30, 128+127*i/3, 10, CV_AA, 5);
      }*/
      
      pos_named["channels"] = c;
      remap(src_dup, cvMat(dst.bind(2, c)), cvMat(maps_cam.bind(2, c)), cv::noArray(), interp);
      
      
      
      //pos_named["channels"] = c;
      //remap(cvMat(src.bind(2, c)), cvMat(dst.bind(2, c)), cvMat(maps_cam.bind(2, c)), cv::noArray(), interp);
    }
  }
  else
    dst = src;
}

bool DepthDist::operator==(const Tree_Derived & rhs) const
{
  const DepthDist * other = dynamic_cast<const DepthDist*>(&rhs);
  
  if (other && ((other->_depth == _depth) || (isnan(other->_depth) && isnan(_depth))))
    return true;
  
  return false; 
}
  
}