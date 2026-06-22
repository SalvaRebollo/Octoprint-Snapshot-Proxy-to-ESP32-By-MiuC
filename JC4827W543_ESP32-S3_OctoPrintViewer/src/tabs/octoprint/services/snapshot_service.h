#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * OctoPrint snapshot engine: downloads the JPEG from the proxy, decodes it into
 * PSRAM using double buffering, and delivers it to the main thread. Isolates all
 * network, decoding, and concurrency concerns (FreeRTOS task + critical sections).
 *
 * The background task NEVER touches LVGL: it only makes the frame ready, and the
 * main thread applies it via takeReadyFrame()/commitFrame().
 */
namespace SnapshotService {

// Parameters for a snapshot request, already validated by the UI.
struct Request {
  char ip[16];
  uint16_t port;
  uint16_t width;
  uint16_t height;
  uint8_t quality;
  float zoom;
  float x;
  float y;
  uint32_t cacheBust;
};

// Read-only view of a decoded frame that is ready to display.
struct FrameView {
  const uint16_t *pixels;
  uint16_t width;
  uint16_t height;
};

// Allocates the PSRAM buffers. Must be called once before startWorker().
bool begin();

// Creates the download/decode task. Returns false if the task could not be created.
bool startWorker();

// Queues a request if the engine is idle. Returns true if it was accepted.
bool request(const Request &request);

/*
 * Main thread. If a valid frame is ready, fills out and returns true without
 * releasing the buffer yet; call commitFrame() after painting it in LVGL.
 * Frames with invalid dimensions are discarded internally (returns false).
 */
bool takeReadyFrame(FrameView &out);

// Confirms the frame delivered by takeReadyFrame() and releases the engine.
void commitFrame();

// Main thread. Copies the latest status emitted by the task; returns true if it changed.
bool takeStatus(char *buffer, size_t capacity);

// Builds the full request URL (used to display it on screen).
void buildUrl(const Request &request, char *url, size_t capacity);

}  // namespace SnapshotService
