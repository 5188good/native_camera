import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'native_camera_platform_interface.dart';

/// An implementation of [NativeCameraPlatform] that uses method channels.
class MethodChannelNativeCamera extends NativeCameraPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('native_camera');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }
}
