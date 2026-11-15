// Emscripten HTML5 input -> ARBaseApp hooks (#2 phase 4): touch, mouse and window resize.
// Coordinates are drawing-buffer pixels of the canvas (clientToCanvas in core/ARInput.h).

#include "core/ARBaseApp.h"
#include "core/ARInput.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>

// The canvas element's client rectangle (left, top, width, height) into out[0..3].
EM_JS(int, webar_canvas_rect, (const char *selector, double *out), {
  var element = document.querySelector(UTF8ToString(selector));
  if (!element) return 0;
  var rect = element.getBoundingClientRect();
  HEAPF64[(out >> 3) + 0] = rect.left;
  HEAPF64[(out >> 3) + 1] = rect.top;
  HEAPF64[(out >> 3) + 2] = rect.width;
  HEAPF64[(out >> 3) + 3] = rect.height;
  return 1;
});

struct ARInputBridge {
  static ARBaseApp *app;
  static std::string selector;
  static int lastWidth;
  static int lastHeight;

  // Client coordinates to canvas pixels; false on a letterbox bar or outside the canvas.
  static bool toCanvas(double clientX, double clientY, float &x, float &y) {
    double rect[4];
    int width = 0, height = 0;
    if (!webar_canvas_rect(selector.c_str(), rect) ||
        emscripten_get_canvas_element_size(selector.c_str(), &width, &height) !=
            EMSCRIPTEN_RESULT_SUCCESS) {
      return false;
    }
    return clientToCanvas(clientX, clientY, rect[0], rect[1], rect[2], rect[3], width, height, x,
                          y);
  }

  static EM_BOOL onTouch(int eventType, const EmscriptenTouchEvent *event, void *) {
    for (int i = 0; i < event->numTouches; ++i) {
      const EmscriptenTouchPoint &touch = event->touches[i];
      if (!touch.isChanged) continue;
      float x, y;
      if (!toCanvas(touch.clientX, touch.clientY, x, y)) continue;
      const int id = static_cast<int>(touch.identifier);
      switch (eventType) {
        case EMSCRIPTEN_EVENT_TOUCHSTART: app->touchDown(x, y, id); break;
        case EMSCRIPTEN_EVENT_TOUCHMOVE: app->touchMoved(x, y, id); break;
        default: app->touchUp(x, y, id); break; // touchend, touchcancel
      }
    }
    return EM_TRUE; // no scrolling or zooming of the page from touches on the canvas
  }

  static EM_BOOL onMouse(int eventType, const EmscriptenMouseEvent *event, void *) {
    float x, y;
    if (!toCanvas(event->clientX, event->clientY, x, y)) return EM_FALSE;
    const int pointerId = 0; // a single mouse pointer
    switch (eventType) {
      case EMSCRIPTEN_EVENT_MOUSEDOWN: app->mouseDown(x, y, event->button, pointerId); break;
      case EMSCRIPTEN_EVENT_MOUSEUP: app->mouseUp(x, y, event->button, pointerId); break;
      default: app->mouseMoved(x, y, pointerId); break;
    }
    return EM_FALSE;
  }

  static EM_BOOL onResize(int, const EmscriptenUiEvent *event, void *) {
    if (event->windowInnerWidth == lastWidth && event->windowInnerHeight == lastHeight) {
      return EM_FALSE; // the same size again: report each change once
    }
    lastWidth = event->windowInnerWidth;
    lastHeight = event->windowInnerHeight;
    app->windowResized(lastWidth, lastHeight);
    return EM_FALSE;
  }
};

ARBaseApp *ARInputBridge::app = nullptr;
std::string ARInputBridge::selector;
int ARInputBridge::lastWidth = -1;
int ARInputBridge::lastHeight = -1;

void ARBaseApp::installInputCallbacks(const std::string &canvasSelector) {
  ARInputBridge::app = this;
  ARInputBridge::selector = canvasSelector;
  const char *target = canvasSelector.c_str();
  emscripten_set_touchstart_callback(target, nullptr, EM_TRUE, ARInputBridge::onTouch);
  emscripten_set_touchmove_callback(target, nullptr, EM_TRUE, ARInputBridge::onTouch);
  emscripten_set_touchend_callback(target, nullptr, EM_TRUE, ARInputBridge::onTouch);
  emscripten_set_touchcancel_callback(target, nullptr, EM_TRUE, ARInputBridge::onTouch);
  emscripten_set_mousedown_callback(target, nullptr, EM_TRUE, ARInputBridge::onMouse);
  emscripten_set_mousemove_callback(target, nullptr, EM_TRUE, ARInputBridge::onMouse);
  emscripten_set_mouseup_callback(target, nullptr, EM_TRUE, ARInputBridge::onMouse);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE,
                                 ARInputBridge::onResize);
}
