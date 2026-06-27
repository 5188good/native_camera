#include "include/native_camera/native_camera_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "native_camera_plugin.h"

void NativeCameraPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  native_camera::NativeCameraPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
