import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'native_camera_method_channel.dart';

abstract class NativeCameraPlatform extends PlatformInterface {
  /// Constructs a NativeCameraPlatform.
  NativeCameraPlatform() : super(token: _token);

  static final Object _token = Object();

  static NativeCameraPlatform _instance = MethodChannelNativeCamera();

  /// The default instance of [NativeCameraPlatform] to use.
  ///
  /// Defaults to [MethodChannelNativeCamera].
  static NativeCameraPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [NativeCameraPlatform] when
  /// they register themselves.
  static set instance(NativeCameraPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}
