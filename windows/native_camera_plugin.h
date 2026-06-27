#ifndef FLUTTER_PLUGIN_NATIVE_CAMERA_PLUGIN_H_
#define FLUTTER_PLUGIN_NATIVE_CAMERA_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>

namespace native_camera {

class NativeCameraPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  NativeCameraPlugin(flutter::PluginRegistrarWindows *registrar);
  virtual ~NativeCameraPlugin();

  NativeCameraPlugin(const NativeCameraPlugin&) = delete;
  NativeCameraPlugin& operator=(const NativeCameraPlugin&) = delete;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

 private:
  flutter::PluginRegistrarWindows *registrar_;
  Microsoft::WRL::ComPtr<IMFMediaSource> media_source_;
  Microsoft::WRL::ComPtr<IMFSourceReader> source_reader_;
  UINT32 device_index_ = 0;
  UINT32 current_width_ = 640;
  UINT32 current_height_ = 480;
  int fps_ = 15;
  std::atomic<bool> is_streaming_{false};
  GUID video_format_ = {0};

  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
  std::mutex sink_mutex_;

  std::vector<std::string> EnumerateCameras();
  std::string OpenCamera(int index);
  std::vector<std::pair<int, int>> GetResolutions(int index);
  std::string SetResolution(int width, int height);
  void StartStream();
  void StopStream();
  void ReadFrame();
  std::string CloseCameraInternal();
};

}  // namespace native_camera

#endif  // FLUTTER_PLUGIN_NATIVE_CAMERA_PLUGIN_H_
