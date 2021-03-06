
#include "dataset.hpp"
#include "calib.hpp"
#include "clif_cv.hpp"
#include "enumtypes.hpp"
#include "preproc.hpp"

#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#ifdef CLIF_WITH_HDMARKER
  #include <hdmarker/hdmarker.hpp>
  #include <hdmarker/subpattern.hpp>
  
  using namespace hdmarker;
#endif
  
#ifdef CLIF_WITH_UCALIB
  #include <ucalib/corr_lines.hpp>
  #include <ucalib/gencam.hpp>
#endif
  
#include "mat.hpp"
  
using namespace std;
using namespace cv;

namespace clif {
  
typedef unsigned int uint;

bool pattern_detect(Dataset *s, cpath imgset, cpath calibset, bool write_debug_imgs)
{
  cpath img_root, map_root;
  if (!s->deriveGroup("calibration/imgs", imgset, "calibration/mapping", calibset, img_root, map_root))
    abort();
  
  Datastore *debug_store = NULL;

  CalibPattern pattern;
  s->getEnum(img_root/"type", pattern);
  
  if (write_debug_imgs && pattern == CalibPattern::HDMARKER)
    debug_store = s->getStore(map_root / "data");
  else
    write_debug_imgs = false;
    
  //FIXME implement generic "format" class?
  Datastore *imgs = s->getStore(img_root/"data");
  assert(imgs);
    
  vector<vector<Point2f>> ipoints;
  vector<vector<Point2f>> wpoints;
  
  //FIXME grayscale for opencv calib!
  int readflags = CVT_8U | DEMOSAIC;
  
  if (pattern == CalibPattern::CHECKERBOARD)
    readflags |= CVT_GRAY;
  
  //FIXME integrate ProcData!
  ProcData proc(readflags, imgs);
  
  int channels = proc.d();
  
  Idx map_size(imgs->dims() - 2);
  map_size[0] = channels;
  for(int i=1;i<map_size.size();i++)
    map_size[i] = imgs->extent()[i+2];
  
  Idx pos(map_size.size());

  
  Mat_<std::vector<Point2f>> wpoints_m(map_size);
  Mat_<std::vector<Point2f>> ipoints_m(map_size);
  
  if (pattern == CalibPattern::CHECKERBOARD) {
    cv::Mat img;
    int size[2];
    
    s->get(img_root / "size", size, 2);
  
    for(;pos<map_size;pos.step(1, map_size)) {
      vector<Point2f> corners;
      std::vector<int> idx(imgs->dims());
      for(int i=3;i<idx.size();i++)
        idx[i] = pos[i-2];
      imgs->readImage(idx, &img, readflags);
      
      cv::Mat ch = clifMat_channel(img, 0);
      
      int succ = findChessboardCorners(ch, Size(size[0],size[1]), corners, CV_CALIB_CB_ADAPTIVE_THRESH+CV_CALIB_CB_NORMALIZE_IMAGE+CALIB_CB_FAST_CHECK+CV_CALIB_CB_FILTER_QUADS);
      
      if (succ) {
        printf("found %6lu corners\n", corners.size());
        cornerSubPix(ch, corners, Size(8,8), Size(-1,-1), TermCriteria(cv::TermCriteria::MAX_ITER | cv::TermCriteria::EPS,100,0.0001));
      }
      else
        printf("found      0 corners\n");
      
      ipoints.push_back(std::vector<Point2f>());
      wpoints.push_back(std::vector<Point2f>());
      
      if (succ) {
        for(int y=0;y<size[1];y++)
          for(int x=0;x<size[0];x++) {
            ipoints.back().push_back(corners[y*size[0]+x]);
            wpoints.back().push_back(Point2f(x,y));
            //pointcount++;
          }
          
        //at the moment single color only!
        pos[0] = 0;
          
        wpoints_m(pos) = wpoints.back();
        ipoints_m(pos) = ipoints.back();
      }
    }
  }
#ifdef CLIF_WITH_HDMARKER
  else if (pattern == CalibPattern::HDMARKER) {
    
    double unit_size; //marker size in mm
    double unit_size_res;
    int recursion_depth;
    cv::Rect limit;
    
    s->get(img_root / "marker_size", unit_size);
    s->get(img_root / "hdmarker_recursion", recursion_depth);
    
    Attribute *bbox_a = s->get(img_root/"bbox");
    if (bbox_a) {
      int bbox_vals[4];
      bbox_a->get(bbox_vals, 4);
      limit = cv::Rect(bbox_vals[0],bbox_vals[1],bbox_vals[2],bbox_vals[3]);
    }
  
    
    //FIXME remove this!
    Marker::init();

    
    for(;pos<map_size;pos.step(1, map_size)) {
      std::vector<int> idx(imgs->dims());
      for(int i=3;i<idx.size();i++)
        idx[i] = pos[i-2];
    
      std::vector<Corner> corners_rough;
      std::vector<Corner> corners;

      bool *mask_ptr = NULL;
      bool masks[3][4];
      
      cv::Mat *debug_img_ptr = NULL;
      cv::Mat debug_img;          
      
      if (imgs->org() == DataOrg::BAYER_2x2) {
        for(int mc=0;mc<3;mc++)
          for(int m=0;m<4;m++)
            masks[mc][m] = false;
          
          switch (imgs->order()) {
            case DataOrder::RGGB : 
              masks[0][0] = true;
              masks[1][1] = true;
              masks[1][2] = true;
              masks[2][3] = true;
              break;
            case DataOrder::GBRG : 
              masks[0][2] = true; 
              masks[1][0] = true;
              masks[1][3] = true;
              masks[2][1] = true;
              break;
            default :
              abort();
          }
          
          printf("process %d x %d\n", pos[1], pos[2]);
          
          cv::Mat debug_imgs[3];
          
          //grayscale rough detection
          //FIXME move this up - mmapped reallocation not possible...
          cv::Mat img;
          imgs->readImage(idx, &img, readflags);
          cv::Mat gray = clifMat_channel(img, 0);
          Marker::detect(gray, corners_rough);
          
          cv::Mat bayer_img;
          imgs->readImage(idx, &bayer_img, CVT_8U);
          cv::Mat bayer = clifMat_channel(bayer_img, 0);
          
          char buf[128];

          for(int c=0;c<channels;c++) {
            if (debug_store)
              debug_img_ptr = &debug_imgs[c];
            
            pos[0] = c;
            
            unit_size_res = unit_size;
            mask_ptr = &masks[c][0];
            hdmarker_detect_subpattern(bayer, corners_rough, corners, recursion_depth, &unit_size_res, debug_img_ptr, mask_ptr, 0, limit);
            
            printf("found %6lu corners for channel %d\n", corners.size(), c);
            
            //sprintf(buf, "debug_img%03d_ch%d.tif", j, c);
            //imwrite(buf, *debug_img_ptr);
            
            std::vector<Point2f> ipoints_v(corners.size());
            std::vector<Point2f> wpoints_v(corners.size());
            
            for(int ci=0;ci<corners.size();ci++) {
              //FIXME multi-channel calib!
              ipoints_v[ci] = corners[ci].p;
              Point2f w_2d = unit_size_res*Point2f(corners[ci].id.x, corners[ci].id.y);
              wpoints_v[ci] = Point2f(w_2d.x, w_2d.y);
            }
            
            wpoints_m(pos) = wpoints_v;
            ipoints_m(pos) = ipoints_v;
            s->flush();
          }
          
          if (debug_store)
            cv::merge(debug_imgs, 3, debug_img);
      }
      else {
        cv::Mat debug_imgs[proc.d()];
        
        //grayscale rough detection
        //FIXME move this up - mmapped reallocation not possible...
        cv::Mat img;
        imgs->readImage(idx, &img, readflags);
        cv::Mat gray = clifMat_channel(img, 0);
        Marker::detect(gray, corners_rough);
        
        cv::Mat img_color;
        imgs->readImage(idx, &img_color, CVT_8U);
        
        
        for(int c=0;c<channels;c++) {
          if (debug_store)
            debug_img_ptr = &debug_imgs[c];
          
          pos[0] = c;
          
          cv::Mat ch = clifMat_channel(img_color, 0);
          
          unit_size_res = unit_size;
          hdmarker_detect_subpattern(ch, corners_rough, corners, recursion_depth, &unit_size_res, debug_img_ptr);
          
          printf("found %6lu corners for channel %d\n", corners.size(), c);
          
          //char buf[128];
          //sprintf(buf, "debug_img%03d_ch%d.tif", j, c);
          //imwrite(buf, *debug_img_ptr);
          
          std::vector<Point2f> ipoints_v(corners.size());
          std::vector<Point2f> wpoints_v(corners.size());
          
          for(int ci=0;ci<corners.size();ci++) {
            //FIXME multi-channel calib!
            ipoints_v[ci] = corners[ci].p;
            Point2f w_2d = unit_size_res*Point2f(corners[ci].id.x, corners[ci].id.y);
            wpoints_v[ci] = Point2f(w_2d.x, w_2d.y);
          }
          
          wpoints_m(pos) = wpoints_v;
          ipoints_m(pos) = ipoints_v;
          s->flush();
        }
        
        if (debug_store) {
          if (proc.d() == 1)
            debug_img = debug_imgs[0];
          else
            cv::merge(debug_imgs, proc.d(), debug_img);
        }
      }
      
      if (debug_store) {
        debug_store->appendImage(&debug_img);
        s->flush();
        
        //char buf[128];
        //sprintf(buf, "col_fit_img%03d.tif", j);
        //imwrite(buf, debug_img);
      }
    }
  }
  #endif
  else
    abort();
  
  s->setAttribute(map_root / "img_points", ipoints_m);
  s->setAttribute(map_root / "world_points", wpoints_m);
  
  std::vector<int> imgsize = { imgSize(imgs).width, imgSize(imgs).height };
  
  s->setAttribute(map_root / "img_size", imgsize);
  
  return false;
}

bool opencv_calibrate(Dataset *set, int flags, cpath map, cpath calib)
{
  cpath map_root, calib_root;
  if (!set->deriveGroup("calibration/mapping", map, "calibration/intrinsics", calib, map_root, calib_root))
    abort();
  
  cv::Mat cam;
  vector<double> dist;
  vector<cv::Mat> rvecs;
  vector<cv::Mat> tvecs;
  int im_size[2];
  
  Mat_<std::vector<Point2f>> wpoints_m;
  Mat_<std::vector<Point2f>> ipoints_m;
    
  vector<vector<Point2f>> ipoints;
  vector<vector<Point3f>> wpoints;
  
  Attribute *w_a = set->get(map_root/"world_points");
  Attribute *i_a = set->get(map_root/"img_points");
  set->get(map_root/"img_size", im_size, 2);
  
  //FIXME error handling
  if (!w_a || !i_a)
    abort();
  
  w_a->get(wpoints_m);
  i_a->get(ipoints_m);
  
  for(int i=0;i<wpoints_m[1];i++) {
    if (!wpoints_m(0, i).size())
      continue;
    
    ipoints.push_back(ipoints_m(0, i));
    wpoints.push_back(std::vector<Point3f>(wpoints_m(0, i).size()));
    for(int j=0;j<wpoints_m(0, i).size();j++)
      wpoints.back()[j] = Point3f(wpoints_m(0, i)[j].x,wpoints_m(0, i)[j].y,0);
  }
  
  double rms = calibrateCamera(wpoints, ipoints, cv::Size(im_size[0],im_size[1]), cam, dist, rvecs, tvecs, flags);
  
  
  printf("opencv calibration rms %f\n", rms);
  
  std::cout << cam << std::endl;
  
  double f[2] = { cam.at<double>(0,0), cam.at<double>(1,1) };
  double c[2] = { cam.at<double>(0,2), cam.at<double>(1,2) };
  
  //FIXME todo delete previous group?!
  set->setAttribute(calib_root / "type", "CV8");
  set->setAttribute(calib_root / "projection", f, 2);
  set->setAttribute(calib_root / "projection_center", c, 2);
  set->setAttribute(calib_root / "opencv_distortion", dist);
  
  return true;
}

#ifdef CLIF_WITH_UCALIB
static void _calib_cam(Mat_<float> &proxy_m, Idx proxy_cam_idx, Idx res_idx, Mat_<float> &corr_line_m, Point2i proxy_size, Mat_<double> &rms, Mat_<double> &proj_rms, Mat_<float> &proj, Mat_<float> &extrinsics_m, Cam_Config cam_config, Calib_Config conf, int im_size[2])
{
  int img_count = proxy_m["views"];
  
  DistCorrLines dist_lines = DistCorrLines(0, 0, 0, cam_config.w, cam_config.h, 100.0, cam_config, conf, proxy_size);
  dist_lines.proxy_backwards.resize(img_count);

  for(auto pos : Idx_It_Dim(proxy_cam_idx, proxy_m, "views")) {
    int pos_int = pos["views"];
    dist_lines.proxy_backwards[pos_int].resize(proxy_size.y*proxy_size.x);
    for(int j=0;j<proxy_size.y;j++)
      for(int i=0;i<proxy_size.x;i++) {
        pos[1] = i;
        pos[2] = j;
        pos[0] = 0;
        dist_lines.proxy_backwards[pos_int][j*proxy_size.x+i].x = proxy_m(pos);
        pos[0] = 1;
        dist_lines.proxy_backwards[pos_int][j*proxy_size.x+i].y = proxy_m(pos);
      }
  }
  
  //Idx cam_pos({proxy_cam_idx.r(proxy_cam_idx.dim("y")+1,proxy_cam_idx.dim("views")-1)});
  
  Idx cam_pos({proxy_cam_idx.r("channels", "cams")});
  
  std::cout << "cam pos:" << cam_pos << std::endl;
  //std::cout << "proj size:" << proj << std::endl;
        
  rms(cam_pos) = dist_lines.proxy_fit_lines_global();
        
  GenCam gcam = GenCam(dist_lines.linefits, cv::Point2i(im_size[0],im_size[1]), cv::Point2i(proxy_m[1],proxy_m[2]));
  
  
  proj(0, cam_pos) = gcam.f.x;
  proj(1, cam_pos) = gcam.f.y;
  proj_rms(cam_pos) = gcam.rms;
  
  for(int j=0;j<proxy_size.y;j++)
    for(int i=0;i<proxy_size.x;i++)
      for(int l=0;l<4;l++) {
        corr_line_m({l, i, j, proxy_cam_idx.r("channels","cams")}) = dist_lines.linefits[j*proxy_size.x+i][l];
        //printf("%d %d %d %d %d = %f\n", l, i, j, proxy_cam_idx["channels"], proxy_cam_idx["cams"], dist_lines.linefits[j*proxy_size.x+i][l]);
      }
  
  for(int img=0;img<img_count;img++) {
    for(int i=0;i<6;i++) {
      extrinsics_m(i, proxy_cam_idx.r("channels", "cams"), img) = dist_lines.extrinsics[6*img+i];
      //std::cout << "set " << Idx({i, proxy_cam_idx.r("channels", "cams"), img}) << std::endl;
    }
  }
}
#endif
  
//FIXME repair!
bool ucalib_calibrate(Dataset *set, cpath proxy, cpath calib)
#ifdef CLIF_WITH_UCALIB
{
  cpath proxy_root, calib_root;
  if (!set->deriveGroup("calibration/proxy", proxy, "calibration/intrinsics", calib, proxy_root, calib_root))
    abort();
  
  int im_size[2];
    
  vector<vector<Point2f>> ipoints;
  vector<vector<Point3f>> wpoints;
  
  Mat_<float> proxy_m;
  Mat_<float> corr_line_m;
  
  set->get(proxy_root/"source/img_size", im_size, 2);
  
  Cam_Config cam_config = { 0.0065, 12.0, 300.0, -1, -1 };
  Calib_Config conf = { true, 190, 420 };

  cam_config.w = im_size[0];
  cam_config.h = im_size[1];
    
  Datastore *proxy_store = set->store(proxy_root/"proxy");
  proxy_store->read(proxy_m);
  
  if (proxy_m.size() == 5) {
    corr_line_m.create({4, proxy_m[1],proxy_m[2], proxy_m[3]});
    
    Point2i proxy_size(proxy_m[1],proxy_m[2]);
    
    std::vector<double> rms(proxy_m[3]);
    std::vector<double> proj_rms(proxy_m[3]);
    Mat_<float> proj(Idx{2, proxy_m[3]});
    Mat_<float> extrinsics_m(Idx{6, proxy_m[3], proxy_m[4]});
    
    for(int color=0;color<proxy_m[3];color++) {
      DistCorrLines dist_lines = DistCorrLines(0, 0, 0, cam_config.w, cam_config.h, 100.0, cam_config, conf, proxy_size);
      dist_lines.proxy_backwards.resize(proxy_m[4]);
      
      Idx pos(proxy_store->dims());
      assert(proxy_store->dims() == 5);
      
      for(int img_n=0;img_n<proxy_m[4];img_n++) {
	dist_lines.proxy_backwards[img_n].resize(proxy_size.y*proxy_size.x);
	for(int j=0;j<proxy_size.y;j++)
	  for(int i=0;i<proxy_size.x;i++) {
	    dist_lines.proxy_backwards[img_n][j*proxy_size.x+i].x = proxy_m(0, i, j, color, img_n);
	    dist_lines.proxy_backwards[img_n][j*proxy_size.x+i].y = proxy_m(1, i, j, color, img_n);
	  }
      }
      
      rms[color] = dist_lines.proxy_fit_lines_global();
      char buf[64];
      sprintf(buf, "center%02d", color);
      dist_lines.Draw(buf);
      
      GenCam gcam = GenCam(dist_lines.linefits, cv::Point2i(im_size[0],im_size[1]), cv::Point2i(proxy_m[1],proxy_m[2]));
      proj(0, color) = gcam.f.x;
      proj(1, color) = gcam.f.y;
      proj_rms[color] = gcam.rms;
      
      for(int j=0;j<proxy_size.y;j++)
	for(int i=0;i<proxy_size.x;i++) {
	  corr_line_m(0, i, j, color) = dist_lines.linefits[j*proxy_size.x+i][0];
	  corr_line_m(1, i, j, color) = dist_lines.linefits[j*proxy_size.x+i][1];
	  corr_line_m(2, i, j, color) = dist_lines.linefits[j*proxy_size.x+i][2];
	  corr_line_m(3, i, j, color) = dist_lines.linefits[j*proxy_size.x+i][3];
	}
	
      for(int img_n=0;img_n<proxy_m[4];img_n++)
	for(int i=0;i<6;i++)
	  extrinsics_m(i, color, img_n) = dist_lines.extrinsics[6*img_n+i];
    }
    
    Datastore *line_store = set->addStore(calib_root/"lines");
    line_store->write(corr_line_m);
    
    set->setAttribute(calib_root/"type", "UCALIB");
    set->setAttribute(calib_root/"rms", rms);
    set->setAttribute(calib_root/"projection_rms", proj_rms);
    set->setAttribute(calib_root/"projection", proj);
    set->setAttribute(calib_root/"extrinsics", extrinsics_m);
    
    return true;
  }
  else { //proxy large than 5
    //last dim is views from same camera!
    
    //FIXME hardcoded for now
    proxy_m.names({"point","x","y","channels","cams","views"});
    
    int step_count = 0;
    cv::Matx31f avg_step(0,0,0);
    
    printf("start ucalib calibration!\n");
    
    int views_dim = proxy_m.size()-1;
    
    Idx corr_size {IR(4, "line"), proxy_m.r("x", "cams")};
    Idx cams_size {proxy_m.r("channels","cams")};
    std::cout << "cams_size: " << cams_size << "\n";
    
    corr_line_m.create(corr_size);
    std::cout << "corr_line_m: " << corr_line_m << "\n";
    
    Point2i proxy_size(proxy_m[1],proxy_m[2]);
    
    Mat_<double> rms_m(cams_size);
    Mat_<double> proj_rms_m(cams_size);
    
    Mat_<float> proj_m({IR(2, "projection"), cams_size});
    
    Idx extrinsics_m_size(cams_size.size()+2);
    extrinsics_m_size[0] = 6;
    for(int i=0;i<cams_size.size();i++)
      extrinsics_m_size[i+1] = cams_size[i];
    extrinsics_m_size[extrinsics_m_size.size()-1] = proxy_m[views_dim];
    
    //TODO first place to need view dimension - rework to make views optional?
    Mat_<float> extrinsics_m({IR(6, "extrinsics"), proxy_m.r("channels","views")});
    
    for(auto pos : Idx_It_Dims(proxy_m, "channels","cams")) {
      std::cout << "process cam:" << pos << std::endl;
      Idx res_idx(cams_size.size());
      for(int i=0;i<res_idx.size();i++)
	res_idx[i] = pos[i+3];
      _calib_cam(proxy_m, pos, res_idx, corr_line_m, proxy_size, rms_m, proj_rms_m, proj_m, extrinsics_m, cam_config,conf, im_size);
      
      cv::Matx31f step;
      if (pos["cams"] > 0) {
	for(int i=3;i<6;i++) {
	  step(i-3) = extrinsics_m({i, pos.r("channels","views")}) - extrinsics_m({i, pos["channels"], pos["cams"]-1, pos["views"]});
	  printf("%f = %f - %f\n", step(i-3), extrinsics_m({i, pos.r("channels","views")}), extrinsics_m({i, pos["channels"], pos["cams"]-1, pos["views"]}));
	}
	printf("step: %fx%fx%f\n", step(0), step(1), step(2));
	avg_step += step;
	step_count++;
      }
      
      printf("\n");
    }
    
    Datastore *line_store = set->addStore(calib_root/"lines");
    line_store->write(corr_line_m);
    
    set->setAttribute(calib_root/"type", "UCALIB");
    set->setAttribute(calib_root/"rms", rms_m);
    set->setAttribute(calib_root/"projection_rms", proj_rms_m);
    set->setAttribute(calib_root/"projection", proj_m);
    set->setAttribute(calib_root/"extrinsics", extrinsics_m);
    
    avg_step *= 1.0/step_count;
    
    printf("average step: %fx%fx%f\n", avg_step(0), avg_step(1), avg_step(2));
    
    
    
    printf("finished ucalib calibration!\n");
    return true;
  }
}
#else
{
  //FIXME report error
  return false;
}
#endif
  
bool generate_proxy_loess(Dataset *set, int proxy_w, int proxy_h , cpath map, cpath proxy)
#ifdef CLIF_WITH_UCALIB
{
  cpath map_root, proxy_root;
  if (!set->deriveGroup("calibration/mapping", map, "calibration/proxy", proxy, map_root, proxy_root))
    abort();
  
  cv::Mat cam;
  vector<double> dist;
  vector<cv::Mat> rvecs;
  vector<cv::Mat> tvecs;
  int im_size[2];
  
  Mat_<std::vector<Point2f>> wpoints_m;
  Mat_<std::vector<Point2f>> ipoints_m;
  
  Mat_<float> proxy_m;
  
  //2-d (color, imgs)
  Attribute *w_a = set->get(map_root/"world_points");
  Attribute *i_a = set->get(map_root/"img_points");
  set->get(map_root/"img_size", im_size, 2);
  
  
  //FIXME error handling
  if (!w_a || !i_a)
    abort();
  
  w_a->get(wpoints_m);
  i_a->get(ipoints_m);
  
  Idx proxy_size(wpoints_m.size()+3);
  proxy_size[0] = 2; // 2d points
  proxy_size[1] = proxy_w;
  proxy_size[2] = proxy_h;
  for(int i=3;i<proxy_size.size();i++)
    proxy_size[i] = wpoints_m[i-3];
  
  proxy_m.create(proxy_size);
  
  Idx map_pos(wpoints_m.size());
  
  Cam_Config cam_config = { 0.0065, 12.0, 300.0, im_size[0], im_size[1] };
  Calib_Config conf = { true, 190, 420 };
  
  for(int color=0;color<wpoints_m[0];color++) {
    
    map_pos = Idx(wpoints_m.size());
    map_pos[0] = color;
    
    for(;map_pos<wpoints_m;map_pos.step(0, wpoints_m)) {
      if (!wpoints_m(map_pos).size())
        continue;
      
      vector<Point2f> ipoints(ipoints_m(map_pos));
      vector<Point3f> wpoints(ipoints_m(map_pos).size());
      
      for(int j=0;j<wpoints_m(map_pos).size();j++)
        wpoints[j] = Point3f(wpoints_m(map_pos)[j].x,wpoints_m(map_pos)[j].y,0);
      
      vector<vector<Point2f>> ipoints_ar(1);
      vector<vector<Point3f>> wpoints_ar(1);
      
      ipoints_ar[0] = ipoints;
      wpoints_ar[0] = wpoints;
      
      DistCorrLines dist_lines = DistCorrLines(0, 0, 0, cam_config.w, cam_config.h, 100.0, cam_config, conf, Point2i(proxy_w,proxy_h));
      dist_lines.add(ipoints_ar, wpoints_ar, 20.0);
      
      std::cout << "fit " << map_pos << std::endl;
      dist_lines.proxy_backwards_poly_generate();
      
      Idx proxy_pos(wpoints_m.size()+3);
      for(int i=0;i<map_pos.size();i++)
        proxy_pos[i+3] = map_pos[i];
      
      for(int j=0;j<proxy_h;j++)
        for(int i=0;i<proxy_w;i++) {
          proxy_pos[1] = i;
          proxy_pos[2] = j;
          proxy_pos[0] = 0;
          proxy_m(proxy_pos) = dist_lines.proxy_backwards[0][j*proxy_w+i].x;
          proxy_pos[0] = 1;
          proxy_m(proxy_pos) = dist_lines.proxy_backwards[0][j*proxy_w+i].y;
        }
    }
  }
  
  Datastore *proxy_store = set->addStore(proxy_root/"proxy");
  proxy_store->write(proxy_m);
  
  return true;
}
#else
{
  //FIXME report error
  return false;
}
#endif

}
