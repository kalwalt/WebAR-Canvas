#ifndef AR_APP_CONFIG_H
#define AR_APP_CONFIG_H

#include <string>
#include <vector>

/**
 * Everything an app needs to start, as plain data (#2): fill it in code, or let
 * ARBaseApp::runApp() apply Module.webarConfig from the page at runtime. AR_APP_MAIN only
 * fills one of these.
 */
struct ARAppConfig {
  std::string canvasSelector = "#canvas";                // the <canvas> for the WebGL context
  int videoWidth = 640;                                  // camera frame size for tracking
  int videoHeight = 480;
  std::string cameraParamPath = "/assets/camera_para.dat";
  std::vector<std::string> markerPaths;                  // NFT datasets, without extension
};

/** Splits newline-separated marker paths ("a\nb\n" -> {"a", "b"}), dropping empty lines. */
inline std::vector<std::string> parseMarkerList(const std::string &lines) {
  std::vector<std::string> paths;
  std::string::size_type start = 0;
  while (start <= lines.size()) {
    std::string::size_type end = lines.find('\n', start);
    if (end == std::string::npos) end = lines.size();
    if (end > start) paths.push_back(lines.substr(start, end - start));
    start = end + 1;
  }
  return paths;
}

#endif // AR_APP_CONFIG_H
