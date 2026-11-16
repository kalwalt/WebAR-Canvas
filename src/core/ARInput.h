#ifndef AR_INPUT_H
#define AR_INPUT_H

/**
 * Pointer coordinates for the input hooks of ARBaseApp (#2 phase 4).
 *
 * The canvas is shown with CSS `object-fit: contain`: the drawing buffer (canvasWidth x
 * canvasHeight pixels, e.g. 640 x 480) is scaled uniformly into the element's box and centred,
 * with bars on two sides. clientToCanvas() maps a pointer's client (CSS) coordinates to
 * drawing-buffer pixels, accounting for both the CSS-to-pixel scale and the bars. It returns
 * false, leaving x and y untouched, for a point on a bar or outside the element.
 */
inline bool clientToCanvas(double clientX, double clientY, double rectLeft, double rectTop,
                           double rectWidth, double rectHeight, int canvasWidth, int canvasHeight,
                           float &x, float &y) {
  if (rectWidth <= 0.0 || rectHeight <= 0.0 || canvasWidth <= 0 || canvasHeight <= 0) {
    return false;
  }
  const double scaleX = rectWidth / canvasWidth;
  const double scaleY = rectHeight / canvasHeight;
  const double scale = scaleX < scaleY ? scaleX : scaleY; // CSS pixels per buffer pixel
  const double drawnWidth = canvasWidth * scale;
  const double drawnHeight = canvasHeight * scale;
  const double localX = clientX - rectLeft - (rectWidth - drawnWidth) * 0.5;
  const double localY = clientY - rectTop - (rectHeight - drawnHeight) * 0.5;
  if (localX < 0.0 || localY < 0.0 || localX >= drawnWidth || localY >= drawnHeight) {
    return false;
  }
  x = static_cast<float>(localX / scale);
  y = static_cast<float>(localY / scale);
  return true;
}

/**
 * The same mapping, clamped to the image's edges (0..canvasWidth, 0..canvasHeight) instead of
 * rejecting points on a bar or outside the element: for up events, which must reach the app
 * even when a finger or the mouse is released off the image. False only while the element or
 * the canvas has no size.
 */
inline bool clientToCanvasClamped(double clientX, double clientY, double rectLeft,
                                  double rectTop, double rectWidth, double rectHeight,
                                  int canvasWidth, int canvasHeight, float &x, float &y) {
  if (rectWidth <= 0.0 || rectHeight <= 0.0 || canvasWidth <= 0 || canvasHeight <= 0) {
    return false;
  }
  const double scaleX = rectWidth / canvasWidth;
  const double scaleY = rectHeight / canvasHeight;
  const double scale = scaleX < scaleY ? scaleX : scaleY;
  double px = (clientX - rectLeft - (rectWidth - canvasWidth * scale) * 0.5) / scale;
  double py = (clientY - rectTop - (rectHeight - canvasHeight * scale) * 0.5) / scale;
  px = px < 0.0 ? 0.0 : (px > canvasWidth ? canvasWidth : px);
  py = py < 0.0 ? 0.0 : (py > canvasHeight ? canvasHeight : py);
  x = static_cast<float>(px);
  y = static_cast<float>(py);
  return true;
}

#endif // AR_INPUT_H
