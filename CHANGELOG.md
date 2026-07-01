## 0.1.0

* Initial release
* Windows camera capture via Media Foundation (NV12/YUY2 → BGRA)
* Camera enumeration, resolution switching, FPS control
* Runtime camera switching
* Raw BGRA frame stream via EventChannel
* Fix: detached streaming thread → joinable (prevents use-after-free on close)
* Fix: StreamController cleaned up on restart (prevents stale listeners)

## 0.0.1

* TODO: Describe initial release.
