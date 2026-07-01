#define NOMINMAX
#include "native_camera_plugin.h"

#include <windows.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <memory>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <algorithm>
#include <set>

#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfobjects.h>
#include <ks.h>
#include <ksmedia.h>
#include <initguid.h>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "ole32")

using Microsoft::WRL::ComPtr;

namespace native_camera {

// Initialize Media Foundation once
bool InitMF() {
  static bool initialized = false;
  if (!initialized) {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    initialized = SUCCEEDED(hr);
  }
  return initialized;
}

// static
void NativeCameraPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto plugin = std::make_unique<NativeCameraPlugin>(registrar);

  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "native_camera",
          &flutter::StandardMethodCodec::GetInstance());
  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  auto event_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar->messenger(), "native_camera/frames",
          &flutter::StandardMethodCodec::GetInstance());

  auto handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      [plugin_pointer = plugin.get()](
          const flutter::EncodableValue* arguments,
          std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
          -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
        std::lock_guard<std::mutex> lock(plugin_pointer->sink_mutex_);
        plugin_pointer->event_sink_ = std::move(events);
        return nullptr;
      },
      [plugin_pointer = plugin.get()](
          const flutter::EncodableValue* arguments)
          -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
        {
          std::lock_guard<std::mutex> lock(plugin_pointer->sink_mutex_);
          plugin_pointer->event_sink_.reset();
        }
        plugin_pointer->StopStream();
        return nullptr;
      });

  event_channel->SetStreamHandler(std::move(handler));
  registrar->AddPlugin(std::move(plugin));
}

NativeCameraPlugin::NativeCameraPlugin(flutter::PluginRegistrarWindows *registrar)
    : registrar_(registrar) {
  InitMF();
}

NativeCameraPlugin::~NativeCameraPlugin() {
  StopStream();
  if (media_source_) media_source_->Shutdown();
  MFShutdown();
}

std::vector<std::string> NativeCameraPlugin::EnumerateCameras() {
  std::vector<std::string> devices;
  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (FAILED(hr)) return devices;

  hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) return devices;

  IMFActivate **activates = nullptr;
  UINT32 count = 0;
  hr = MFEnumDeviceSources(attributes.Get(), &activates, &count);
  if (FAILED(hr)) return devices;

  for (UINT32 i = 0; i < count; i++) {
    WCHAR *name = nullptr;
    UINT32 nameLen = 0;
    hr = activates[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
    if (SUCCEEDED(hr) && name) {
      int size = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
      std::string deviceName(size, 0);
      WideCharToMultiByte(CP_UTF8, 0, name, -1, &deviceName[0], size, nullptr, nullptr);
      if (!deviceName.empty() && deviceName.back() == '\0') deviceName.pop_back();
      devices.push_back(deviceName);
      CoTaskMemFree(name);
    }
    activates[i]->Release();
  }
  CoTaskMemFree(activates);
  return devices;
}

std::string NativeCameraPlugin::OpenCamera(int index) {
  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 2);
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "MFCreateAttributes: 0x%08X", hr);
    return buf;
  }

  hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "SetGUID: 0x%08X", hr);
    return buf;
  }

  IMFActivate **activates = nullptr;
  UINT32 count = 0;
  hr = MFEnumDeviceSources(attributes.Get(), &activates, &count);
  if (FAILED(hr) || index >= (int)count) {
    if (activates) CoTaskMemFree(activates);
    char buf[128]; snprintf(buf, sizeof(buf), "MFEnumDeviceSources: count=%u hr=0x%08X", count, hr);
    return buf;
  }

  hr = activates[index]->ActivateObject(IID_PPV_ARGS(&media_source_));
  for (UINT32 i = 0; i < count; i++) activates[i]->Release();
  CoTaskMemFree(activates);
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "ActivateObject: 0x%08X", hr);
    return buf;
  }

  // Create Source Reader
  hr = MFCreateSourceReaderFromMediaSource(
      media_source_.Get(), nullptr, &source_reader_);
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "MFCreateSourceReader: 0x%08X", hr);
    return buf;
  }

  // Request NV12 explicitly — the most common raw format for C270
  ComPtr<IMFMediaType> requested_type;
  hr = MFCreateMediaType(&requested_type);
  if (SUCCEEDED(hr)) {
    requested_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    requested_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    hr = source_reader_->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, requested_type.Get());
    // If NV12 fails, fall back to native format
    if (FAILED(hr)) {
      source_reader_->SetCurrentMediaType(
          (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, nullptr);
    }
  }

  // Read actual format
  ComPtr<IMFMediaType> native_type;
  if (SUCCEEDED(source_reader_->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &native_type))) {
    native_type->GetGUID(MF_MT_SUBTYPE, &video_format_);
    MFGetAttributeSize(native_type.Get(), MF_MT_FRAME_SIZE, &current_width_, &current_height_);
  }

  device_index_ = index;
  return "";
}

void NativeCameraPlugin::StartStream() {
  if (is_streaming_.load() || !source_reader_) return;
  is_streaming_.store(true);

  int delay_ms = 1000 / std::max(1, std::min(fps_, 60));
  streaming_thread_ = std::thread([this, delay_ms]() {
    while (is_streaming_.load()) {
      ReadFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
  });
}

void NativeCameraPlugin::StopStream() {
  is_streaming_.store(false);
  if (streaming_thread_.joinable()) {
    streaming_thread_.join();
  }
}

void NativeCameraPlugin::ReadFrame() {
  if (!source_reader_) return;

  ComPtr<IMFSample> sample;
  DWORD stream_flags = 0;
  HRESULT hr = source_reader_->ReadSample(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &stream_flags, nullptr, &sample);

  if (FAILED(hr) || (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM)) return;
  if (!sample) return;

  // Get resolution
  ComPtr<IMFMediaType> current_type;
  UINT32 width = 640, height = 480;
  if (SUCCEEDED(source_reader_->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current_type))) {
    MFGetAttributeSize(current_type.Get(), MF_MT_FRAME_SIZE, &width, &height);
  }

  if (width == 0 || height == 0) return;

  // Get buffer and try 2D lock for actual stride
  ComPtr<IMFMediaBuffer> buffer;
  hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) return;

  ComPtr<IMF2DBuffer> buffer2d;
  BYTE *data = nullptr;
  LONG y_stride = (LONG)width;   // fallback: assume width == stride
  bool is_2d = false;
  bool locked = false;

  if (SUCCEEDED(buffer.As(&buffer2d))) {
    hr = buffer2d->Lock2D(&data, &y_stride);
    if (SUCCEEDED(hr) && data) {
      is_2d = true;
      locked = true;
    }
  }

  if (!locked) {
    DWORD max_len = 0, cur_len = 0;
    hr = buffer->Lock(&data, &max_len, &cur_len);
    if (FAILED(hr) || !data || cur_len == 0) {
      buffer->Unlock();
      return;
    }
    locked = true;
  }

  // Convert to BGRA
  std::vector<uint8_t> rgba(width * height * 4);
  auto clamp = [](int v) -> BYTE { return (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v)); };

  if (video_format_ == MFVideoFormat_NV12 || video_format_.Data1 == 0) {
    // NV12: Y plane at data, UV plane after Y plane (stride * height bytes offset)
    BYTE *y_plane = data;
    BYTE *uv_plane = data + y_stride * height;

    for (UINT32 y = 0; y < height; y++) {
      for (UINT32 x = 0; x < width; x++) {
        int y_val = y_plane[y * y_stride + x];
        int uv_offset = (y / 2) * y_stride + (x & ~1);
        int u_val = uv_plane[uv_offset] - 128;
        int v_val = uv_plane[uv_offset + 1] - 128;

        // BT.601 full range
        int r = y_val + ((359 * v_val) >> 8);
        int g = y_val - ((88 * u_val + 183 * v_val) >> 8);
        int b = y_val + ((454 * u_val) >> 8);

        int out_i = (y * width + x) * 4;
        rgba[out_i]     = clamp(b);
        rgba[out_i + 1] = clamp(g);
        rgba[out_i + 2] = clamp(r);
        rgba[out_i + 3] = 255;
      }
    }
  } else if (video_format_ == MFVideoFormat_YUY2) {
    // YUY2: Y0 U0 Y1 V0 — 2 pixels per 4 bytes
    for (UINT32 y = 0; y < height; y++) {
      for (UINT32 x = 0; x < width; x += 2) {
        int px_offset = y * y_stride + x * 2;
        int y0 = data[px_offset];
        int u  = data[px_offset + 1] - 128;
        int y1 = data[px_offset + 2];
        int v  = data[px_offset + 3] - 128;

        int r = y0 + ((359 * v) >> 8);
        int g = y0 - ((88 * u + 183 * v) >> 8);
        int b = y0 + ((454 * u) >> 8);

        int out0 = (y * width + x) * 4;
        rgba[out0]     = clamp(b);
        rgba[out0 + 1] = clamp(g);
        rgba[out0 + 2] = clamp(r);
        rgba[out0 + 3] = 255;

        if (x + 1 < width) {
          r = y1 + ((359 * v) >> 8);
          g = y1 - ((88 * u + 183 * v) >> 8);
          b = y1 + ((454 * u) >> 8);

          int out1 = (y * width + x + 1) * 4;
          rgba[out1]     = clamp(b);
          rgba[out1 + 1] = clamp(g);
          rgba[out1 + 2] = clamp(r);
          rgba[out1 + 3] = 255;
        }
      }
    }
  } else {
    // Unknown format — fallback: try NV12
    BYTE *y_plane = data;
    BYTE *uv_plane = data + y_stride * height;
    for (UINT32 y = 0; y < height; y++) {
      for (UINT32 x = 0; x < width; x++) {
        int y_val = y_plane[y * y_stride + x];
        int uv_offset = (y / 2) * y_stride + (x & ~1);
        int u_val = uv_plane[uv_offset] - 128;
        int v_val = uv_plane[uv_offset + 1] - 128;

        int r = y_val + ((359 * v_val) >> 8);
        int g = y_val - ((88 * u_val + 183 * v_val) >> 8);
        int b = y_val + ((454 * u_val) >> 8);

        int out_i = (y * width + x) * 4;
        rgba[out_i]     = clamp(b);
        rgba[out_i + 1] = clamp(g);
        rgba[out_i + 2] = clamp(r);
        rgba[out_i + 3] = 255;
      }
    }
  }

  // Unlock buffer
  if (is_2d) buffer2d->Unlock2D();
  else buffer->Unlock();

  // Thread-safe send
  {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    if (!event_sink_) return;

    flutter::EncodableMap frame;
    frame[flutter::EncodableValue("width")] = flutter::EncodableValue((int)width);
    frame[flutter::EncodableValue("height")] = flutter::EncodableValue((int)height);
    frame[flutter::EncodableValue("bytes")] = flutter::EncodableValue(rgba);
    event_sink_->Success(flutter::EncodableValue(frame));
  }
}

std::string NativeCameraPlugin::CloseCameraInternal() {
  if (media_source_) {
    media_source_->Shutdown();
    media_source_.Reset();
  }
  source_reader_.Reset();
  video_format_ = {0};
  return "";
}

std::vector<std::pair<int, int>> NativeCameraPlugin::GetResolutions(int index) {
  std::vector<std::pair<int, int>> resolutions;
  
  // Enumerate device to get its activate pointer
  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (FAILED(hr)) return resolutions;
  
  hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) return resolutions;
  
  IMFActivate **activates = nullptr;
  UINT32 count = 0;
  hr = MFEnumDeviceSources(attributes.Get(), &activates, &count);
  if (FAILED(hr) || index >= (int)count) {
    if (activates) CoTaskMemFree(activates);
    return resolutions;
  }
  
  // Activate device temporarily to get media types
  ComPtr<IMFMediaSource> temp_source;
  hr = activates[index]->ActivateObject(IID_PPV_ARGS(&temp_source));
  for (UINT32 i = 0; i < count; i++) activates[i]->Release();
  CoTaskMemFree(activates);
  if (FAILED(hr)) return resolutions;
  
  ComPtr<IMFSourceReader> temp_reader;
  hr = MFCreateSourceReaderFromMediaSource(temp_source.Get(), nullptr, &temp_reader);
  if (FAILED(hr)) {
    temp_source->Shutdown();
    return resolutions;
  }
  
  // Enumerate native media types
  std::set<std::pair<int, int>> seen;
  for (DWORD i = 0; ; i++) {
    ComPtr<IMFMediaType> media_type;
    hr = temp_reader->GetNativeMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &media_type);
    if (FAILED(hr)) break;
    
    GUID subtype;
    if (SUCCEEDED(media_type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
      UINT32 w = 0, h = 0;
      MFGetAttributeSize(media_type.Get(), MF_MT_FRAME_SIZE, &w, &h);
      if (w > 0 && h > 0) {
        auto p = std::make_pair((int)w, (int)h);
        if (seen.find(p) == seen.end()) {
          seen.insert(p);
          resolutions.push_back(p);
        }
      }
    }
  }
  
  temp_reader.Reset();
  temp_source->Shutdown();
  
  // Sort: largest first
  std::sort(resolutions.begin(), resolutions.end(),
    [](const auto& a, const auto& b) { return a.first * a.second > b.first * b.second; });
  
  return resolutions;
}

std::string NativeCameraPlugin::SetResolution(int width, int height) {
  if (!source_reader_) return "Camera not opened";
  if (is_streaming_.load()) return "Stop streaming before changing resolution";
  
  ComPtr<IMFMediaType> media_type;
  HRESULT hr = MFCreateMediaType(&media_type);
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "MFCreateMediaType: 0x%08X", hr);
    return buf;
  }
  
  media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(media_type.Get(), MF_MT_FRAME_SIZE, width, height);
  
  hr = source_reader_->SetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, media_type.Get());
  if (FAILED(hr)) {
    char buf[64]; snprintf(buf, sizeof(buf), "SetCurrentMediaType: 0x%08X", hr);
    return buf;
  }
  
  current_width_ = width;
  current_height_ = height;
  return "";
}

void NativeCameraPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

  const auto &method = method_call.method_name();

  if (method.compare("getPlatformVersion") == 0) {
    result->Success(flutter::EncodableValue("Windows 10+"));
  } else if (method.compare("getCameras") == 0) {
    auto devices = EnumerateCameras();
    flutter::EncodableList list;
    for (const auto &name : devices) {
      list.push_back(flutter::EncodableValue(name));
    }
    result->Success(flutter::EncodableValue(list));
  } else if (method.compare("getResolutions") == 0) {
    int index = 0;
    const auto *args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto it = args->find(flutter::EncodableValue("cameraIndex"));
      if (it != args->end()) index = std::get<int>(it->second);
    }
    auto resolutions = GetResolutions(index);
    flutter::EncodableList list;
    for (const auto &[w, h] : resolutions) {
      flutter::EncodableMap res;
      res[flutter::EncodableValue("width")] = flutter::EncodableValue(w);
      res[flutter::EncodableValue("height")] = flutter::EncodableValue(h);
      list.push_back(flutter::EncodableValue(res));
    }
    result->Success(flutter::EncodableValue(list));
  } else if (method.compare("setResolution") == 0) {
    int width = 640, height = 480;
    const auto *args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto w = args->find(flutter::EncodableValue("width"));
      auto h = args->find(flutter::EncodableValue("height"));
      if (w != args->end()) width = std::get<int>(w->second);
      if (h != args->end()) height = std::get<int>(h->second);
    }
    std::string err = SetResolution(width, height);
    if (err.empty()) {
      result->Success(flutter::EncodableValue(true));
    } else {
      result->Error("CAMERA_ERROR", err);
    }
  } else if (method.compare("setFps") == 0) {
    int fps = 15;
    const auto *args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto it = args->find(flutter::EncodableValue("fps"));
      if (it != args->end()) fps = std::get<int>(it->second);
    }
    fps_ = std::max(1, std::min(fps, 60));
    result->Success(flutter::EncodableValue(fps_));
  } else if (method.compare("switchCamera") == 0) {
    int index = 0;
    const auto *args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto it = args->find(flutter::EncodableValue("index"));
      if (it != args->end()) index = std::get<int>(it->second);
    }
    StopStream();
    std::string err = CloseCameraInternal();
    if (!err.empty()) {
      result->Error("CAMERA_ERROR", err);
      return;
    }
    err = OpenCamera(index);
    if (err.empty()) {
      result->Success(flutter::EncodableValue(true));
    } else {
      result->Error("CAMERA_ERROR", err);
    }
  } else if (method.compare("openCamera") == 0) {
    int index = 0;
    const auto *args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto it = args->find(flutter::EncodableValue("index"));
      if (it != args->end()) index = std::get<int>(it->second);
    }
    std::string err = OpenCamera(index);
    if (err.empty()) {
      result->Success(flutter::EncodableValue(true));
    } else {
      result->Error("CAMERA_ERROR", err);
    }
  } else if (method.compare("startStream") == 0) {
    StartStream();
    result->Success(flutter::EncodableValue(true));
  } else if (method.compare("stopStream") == 0) {
    StopStream();
    result->Success(flutter::EncodableValue(true));
  } else if (method.compare("closeCamera") == 0) {
    StopStream();
    CloseCameraInternal();
    result->Success(flutter::EncodableValue(true));
  } else {
    result->NotImplemented();
  }
}

}  // namespace native_camera
