# Camera Widget Design Document

## Overview

Add an MJPEG camera stream viewer to SmartDashboard, displayed as a dockable
panel.  The viewer consumes standard MJPEG-over-HTTP streams (the same format
served by cscore / mjpg-streamer on FRC robots) and renders frames in a Qt
widget with an optional HUD-style vector graphics overlay.

### Goals

1. **MJPEG display** -- consume `http://<ip>:<port>/?action=stream` and show
   live video in a resizable, dockable panel.
2. **NT4 CameraPublisher auto-discovery** -- subscribe to
   `/CameraPublisher/` via NT4 and automatically populate a camera selector
   from advertised stream URLs.
3. **Targeting reticle overlay** -- dashboard-side fighter-jet style
   targeting reticle (crosshair + circle) drawn via QPainter on top of the
   video widget.  Resolution-independent, click-drag positionable.  This is
   a SmartDashboard UI feature, independent of what the video source shows.
4. **Abstracted frame source** -- the display widget accepts decoded QImages
   from any backend.  MJPEG HTTP is the first; Robot_Simulation serves
   MJPEG from its OSG 3D viewer on port 1181, discovered identically to
   a real camera via CameraPublisher keys on its NT4 server (port 5810).

Note: The Honda backup camera-style guide lines (curved path overlay driven
by velocity/angular velocity, inspired by 2014 Compositor.cpp PathRenderer)
are a **simulator-side** feature drawn in OSG and baked into the MJPEG
frames before they reach the dashboard.  That is a Robot_Simulation feature,
not a SmartDashboard feature.  The two overlays serve different purposes and
live in different codebases.

### Non-goals (explicitly out of scope)

- In-dashboard computer vision / image processing (that belongs on
  robot-side coprocessors).
- H.264 / RTSP decoding (would require FFmpeg; deferred until needed).
- ProcAmp color correction (2014 feature; no modern use case).
- Camera property control via NT4 CameraPublisher Property/ keys (can be
  added later as an enhancement).

---

## Architecture

```
 NT4 Transport                    Manual URL entry
      |                                  |
      v                                  v
 CameraPublisher              CameraStreamSource
 Discovery                     (abstract interface)
 (reads /CameraPublisher/              |
  subtree, extracts             +--------------+
  stream URLs)                  |              |
                                v              v
                        MjpegStreamSource   (future: SimFrameSource)
                          |
                          | QImage frames via signal
                          v
                    CameraViewerDock
                     (QDockWidget)
                          |
                          +-- CameraDisplayWidget (custom QWidget, paintEvent)
                          |     +-- aspect-ratio-aware frame rendering
                          |     +-- HUD overlay (QPainter vector graphics)
                          +-- toolbar: camera selector combo, URL field,
                                       connect/disconnect, HUD toggle
```

### Key classes

| Class | File | Role |
|---|---|---|
| `CameraViewerDock` | `src/widgets/camera_viewer_dock.h/.cpp` | QDockWidget container; toolbar + display; lifecycle management |
| `CameraDisplayWidget` | `src/widgets/camera_display_widget.h/.cpp` | Custom QWidget; paints current frame + HUD overlay via QPainter |
| `MjpegStreamSource` | `src/camera/mjpeg_stream_source.h/.cpp` | HTTP client that connects to MJPEG stream, parses multipart boundaries, decodes JPEG frames, emits QImage signal |
| `CameraStreamSource` | `src/camera/camera_stream_source.h` | Abstract base / interface: `Start(url)`, `Stop()`, `FrameReady(QImage)` signal |
| `CameraPublisherDiscovery` | `src/camera/camera_publisher_discovery.h/.cpp` | Monitors NT4 `/CameraPublisher/` keys; emits camera-list-changed signal |

---

## Component Details

### 1. MjpegStreamSource

**Protocol**: MJPEG-over-HTTP uses a `multipart/x-mixed-replace` content type.
Each frame is a JPEG image delimited by a boundary string in the HTTP headers.

```
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=--myboundary

--myboundary
Content-Type: image/jpeg
Content-Length: 12345

<JPEG bytes>
--myboundary
Content-Type: image/jpeg
Content-Length: 23456

<JPEG bytes>
...
```

**Implementation approach**:

- Use `QNetworkAccessManager` + `QNetworkReply` (already linked: `Qt6::Network`).
- Connect to `QNetworkReply::readyRead` to incrementally read the response body.
- Parse the multipart boundary from the `Content-Type` header.
- Buffer incoming data, scan for boundary markers, extract per-frame JPEG
  payloads.
- Decode each JPEG payload via `QImage::loadFromData(bytes, "JPEG")` -- Qt's
  built-in JPEG plugin handles this with no additional dependencies.
- Emit `FrameReady(QImage)` signal on successful decode.
- Handle `Content-Length` headers within each part when available, but also
  support the boundary-scan fallback (some cameras omit per-part Content-Length).

**Threading**: `QNetworkAccessManager` is event-driven and runs on the Qt
event loop -- no dedicated thread needed for the HTTP client.  JPEG decode
via `QImage::loadFromData` is fast enough for MJPEG resolutions (typically
320x240 to 640x480 at 15-30 fps) on the main thread.

Ian: If profiling shows frame decode blocking the UI at high resolutions,
the decode step can be moved to a worker thread with a signal/slot
connection.  Don't over-engineer this until there's a measured problem.

**Reconnection**: On stream error or disconnect, emit a `Disconnected`
signal.  The dock's connect/disconnect button handles manual retry.
Auto-reconnect timer (configurable, default 2s) can be added as an
enhancement.

### 2. CameraDisplayWidget

A `QWidget` subclass that:

1. **Stores the latest QImage** received from the stream source.
2. **paintEvent()** draws the frame scaled to fit the widget while preserving
   aspect ratio (letterbox/pillarbox with black bars).
3. **Paints the targeting reticle overlay** on top of the frame using
   `QPainter` with antialiasing enabled and a green pen (`QColor(0, 255, 0)`
   or a configurable color).

Ian: The targeting reticle is a dashboard-side feature drawn over whatever
video the widget displays.  It is completely independent of the Honda backup
camera-style guide lines, which are a simulator-side feature drawn in OSG
and baked into the MJPEG frames.  The two overlays serve different purposes
in different codebases.

**Reticle elements (v1)**:

- **Targeting reticle**: crosshair + circle, position stored as normalized
  (0..1, 0..1) coordinates so it's resolution-independent.
- Position adjustable by click-drag on the overlay.
- Future HUD elements (horizon line, heading tape, distance readout) can be
  added as additional QPainter draw calls.

**Frame rate display**: Optional FPS counter in the corner (measured from
`FrameReady` signal frequency).

### 3. CameraViewerDock

A `QDockWidget` (follows the same pattern as `RunBrowserDock`):

- **Object name**: `"cameraViewerDock"` (for Qt state save/restore).
- **Allowed areas**: `Qt::AllDockWidgetAreas` (camera can be useful anywhere).
- **Features**: `DockWidgetMovable | DockWidgetFloatable | DockWidgetClosable`.
- **Default position**: `Qt::RightDockWidgetArea`, starts hidden.
- **View menu entry**: "Camera" checkbox in the View menu, wired the same
  way as "Run Browser" (toggled action <-> visibilityChanged sync).

**Toolbar contents**:

| Widget | Purpose |
|---|---|
| `QComboBox` | Camera selector (populated from CameraPublisher discovery or manual entries) |
| `QLineEdit` | Manual URL input (e.g. `http://10.12.34.2:1181/?action=stream`) |
| `QPushButton` Connect | Start streaming from selected/entered URL |
| `QPushButton` Disconnect | Stop current stream |
| `QCheckBox` Reticle | Toggle targeting reticle overlay visibility |

### 4. CameraPublisherDiscovery

Monitors NT4 keys under `/CameraPublisher/` to auto-discover cameras.

**Key schema** (from WPILib CameraServer):

```
/CameraPublisher/{CameraName}/source       -> string  (e.g. "usb:/dev/video0")
/CameraPublisher/{CameraName}/description  -> string  (human-readable)
/CameraPublisher/{CameraName}/connected    -> boolean
/CameraPublisher/{CameraName}/streams      -> string[] (URLs with prefix)
/CameraPublisher/{CameraName}/mode         -> string  (e.g. "640x480 MJPEG 30fps")
```

**Stream URL format**: `mjpg:http://{address}:{port}/?action=stream`
- Strip the `mjpg:` prefix to get the raw HTTP URL.
- Base port is 1181; increments for each additional camera server.

**Implementation**:

- Receives NT4 key updates via the existing transport callback mechanism.
- Watches for keys matching `/CameraPublisher/*/streams`.
- Parses the string array, strips `mjpg:` prefixes, and emits
  `CamerasChanged(QStringList names, QMap<QString, QStringList> urlsByName)`.
- The dock populates its combo box from this signal.

Ian: CameraPublisherDiscovery is deliberately decoupled from the NT4
transport plugin.  It consumes the same key-update callbacks that tiles
consume -- MainWindow routes updates to it.  This avoids linking the
camera feature to a specific transport.

### 5. MainWindow Integration

Minimal integration following the RunBrowserDock pattern:

```cpp
// In MainWindow members:
QAction* m_cameraViewAction = nullptr;
sd::widgets::CameraViewerDock* m_cameraDock = nullptr;

// In SetupUi (View menu, near Run Browser):
m_cameraViewAction = viewMenu->addAction("Camera");
m_cameraViewAction->setCheckable(true);
m_cameraViewAction->setChecked(false);

// In dock creation (after Run Browser dock):
m_cameraDock = new sd::widgets::CameraViewerDock(this);
addDockWidget(Qt::RightDockWidgetArea, m_cameraDock);
m_cameraDock->setVisible(false);
// Toggle wiring (same pattern as Run Browser)
connect(m_cameraViewAction, &QAction::toggled, ...);
connect(m_cameraDock, &QDockWidget::visibilityChanged, ...);
```

---

## Dependencies

**No new external dependencies required.**

| Need | Solution |
|---|---|
| HTTP client | `QNetworkAccessManager` (Qt6::Network, already linked) |
| JPEG decode | `QImage::loadFromData()` (Qt built-in, imageformats plugin already deployed) |
| 2D rendering | `QPainter` (Qt built-in) |

---

## File Plan

New files to create:

```
SmartDashboard/src/camera/
    camera_stream_source.h         -- abstract base (Start/Stop/FrameReady)
    mjpeg_stream_source.h          -- MJPEG HTTP stream reader (header)
    mjpeg_stream_source.cpp        -- MJPEG HTTP stream reader (implementation)
    camera_publisher_discovery.h   -- NT4 CameraPublisher key watcher (header)
    camera_publisher_discovery.cpp -- NT4 CameraPublisher key watcher (impl)
SmartDashboard/src/widgets/
    camera_viewer_dock.h           -- dock widget container (header)
    camera_viewer_dock.cpp         -- dock widget container (implementation)
    camera_display_widget.h        -- custom paint widget (header)
    camera_display_widget.cpp      -- custom paint widget (implementation)
SmartDashboard/tests/
    mjpeg_stream_source_tests.cpp  -- unit tests for MJPEG parser
    camera_display_widget_tests.cpp -- unit tests for display widget
```

Files to modify:

```
SmartDashboard/CMakeLists.txt      -- add new source files to app + test targets
SmartDashboard/src/app/main_window.h   -- add dock member + action
SmartDashboard/src/app/main_window.cpp -- create dock, wire menu, route NT4 keys
```

---

## Implementation Order

### Phase 1: MJPEG Stream Reader + Display Widget (MVP)

1. Create `CameraStreamSource` abstract interface
2. Implement `MjpegStreamSource` with `QNetworkAccessManager`
3. Create `CameraDisplayWidget` with aspect-ratio-aware `paintEvent()`
4. Create `CameraViewerDock` with manual URL input + connect/disconnect
5. Wire into `MainWindow` (View menu, dock creation)
6. Write unit tests for MJPEG boundary parser
7. Manual test with a real camera or test MJPEG server

### Phase 2: Targeting Reticle Overlay (Dashboard-Side)

8. Add reticle rendering to `CameraDisplayWidget::paintEvent()`
9. Implement targeting reticle (crosshair + circle) with click-drag positioning
10. Add reticle toggle checkbox to dock toolbar
11. Persist reticle position in QSettings

Ian: The targeting reticle is a dashboard UI feature drawn over the video
widget via QPainter.  It is completely separate from the Honda backup
camera-style guide lines, which are a simulator-side feature (Phase 5).

### Phase 3: NT4 CameraPublisher Discovery

12. Implement `CameraPublisherDiscovery`
13. Wire NT4 key updates from MainWindow to discovery
14. Populate camera selector combo from discovered cameras
15. Auto-select first discovered camera (or persist last selection)

### Phase 4: Robot Simulation MJPEG Server (Robot_Simulation repo)

16. Add OSG post-draw callback for framebuffer readback in OSG_Viewer
17. JPEG encode captured frames (stb_image_write.h or osgDB)
18. Implement MJPEG HTTP server on port 1181 using IXWebSocket (already a
    dependency in Robot_Simulation)
19. Publish CameraPublisher keys via the existing NT4 server on port 5810
20. Dashboard auto-discovers the simulator camera identically to a real camera
    -- zero special-case code on the dashboard side

### Phase 5: Backup Camera Guide Lines (Robot_Simulation repo, OSG-side)

21. Implement PathRenderer-style overlay **in OSG on the simulator side**
    using OSG's line drawing primitives (`osg::Geometry` with `GL_LINES`)
22. Read velocity and angular velocity from the simulation to curve the path
    lines (same math as 2014 Compositor.cpp `ComputePathPoints()`)
23. Overlay is composited into the 3D scene before framebuffer capture, so
    MJPEG frames include the overlay with no dashboard-side drawing needed
24. Iterate: distance markers that animate with velocity, Star Wars Arcade
    80s wireframe perspective effects

Ian: Phase 5 is entirely in the Robot_Simulation codebase.  The dashboard
has no awareness of it — it just displays whatever frames the MJPEG stream
delivers.  The two overlay features (dashboard reticle vs. simulator guide
lines) serve different purposes in different codebases.

---

## Robot Simulation MJPEG Server Architecture

**Decision: Option A** -- The simulator serves MJPEG over HTTP (separate
from NT4 transport), and publishes CameraPublisher keys via its existing
NT4 server so the dashboard discovers it identically to a real camera.

```
Robot_Simulation                          SmartDashboard
+-------------------+                    +--------------------+
| OSG_Viewer        |                    | CameraPublisher    |
|  3D scene         |                    |  Discovery         |
|  (+ optional      |                    |  (reads /Camera    |
|   guide lines)    |                    |   Publisher/ keys) |
|  post-draw CB     |                    +--------+-----------+
|  -> framebuffer   |
|  -> JPEG encode   |
|  -> MJPEG HTTP    |--- port 1181 -->   | MjpegStreamSource  |
|     server        |   MJPEG stream     |  -> QImage frames  |
+-------------------+                    |  -> CameraDisplay  |
| NT4 Server        |--- port 5810 -->   |     Widget         |
|  publishes:       |   NT4 WebSocket    +--------------------+
|  /CameraPublisher |
|   /SimCamera/     |
|   streams, etc.   |
+-------------------+
```

**Backup camera guide lines on OSG side** -- The 2014 Compositor.cpp
PathRenderer's 3D math (which already uses `osg::Vec3d`) maps directly to
OSG line drawing.  Drawing the guide lines in OSG before framebuffer
capture means:
- The composed frame (3D scene + guide lines) streams as a single image
- No duplicate projection math on the dashboard side
- The dashboard stays a pure video viewer with no vision processing
- OSG has excellent line drawing functions (the user's words)

---

## Design Decisions & Rationale

**Why a dock, not a tile?**
Camera video is a fundamentally different display from data tiles.  It needs
a large, resizable viewing area and doesn't map to a single NT key-value
pair.  A dock can be floated to a second monitor (useful for driver stations
with multiple screens), tabbed alongside the Run Browser, or hidden entirely.

**Why QNetworkAccessManager instead of raw sockets?**
It handles HTTP protocol details (headers, chunked transfer, redirects,
connection management) and integrates natively with Qt's event loop.  No
threading needed.  The MJPEG multipart boundary parsing is the only custom
protocol code.

**Why not a separate thread for frame decode?**
At typical FRC camera resolutions (320x240 to 640x480), JPEG decode via
Qt's built-in plugin takes <1ms per frame.  A dedicated thread adds
complexity without measurable benefit.  If higher resolutions are needed
later, the `FrameReady` signal can be connected across thread boundaries
with `Qt::QueuedConnection`.

**Why an abstract CameraStreamSource?**
The display widget doesn't need to know where frames come from.  The
abstract interface makes it trivial to add a Robot_Simulation frame source,
a test-pattern generator (for development/testing), or a file-based replay
source later.

**Why keep CameraPublisherDiscovery separate from the NT4 plugin?**
The NT4 transport plugin provides raw key-value transport.  Camera discovery
is a higher-level concern that interprets specific key patterns.  Keeping
it in the app layer means it works with any transport that delivers
`/CameraPublisher/` keys, and the plugin stays focused on protocol.
