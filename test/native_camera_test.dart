import 'package:flutter_test/flutter_test.dart';
import 'package:native_camera/native_camera.dart';
import 'package:native_camera/native_camera_platform_interface.dart';
import 'package:native_camera/native_camera_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockNativeCameraPlatform
    with MockPlatformInterfaceMixin
    implements NativeCameraPlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final NativeCameraPlatform initialPlatform = NativeCameraPlatform.instance;

  test('$MethodChannelNativeCamera is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelNativeCamera>());
  });

  test('getPlatformVersion', () async {
    NativeCamera nativeCameraPlugin = NativeCamera();
    MockNativeCameraPlatform fakePlatform = MockNativeCameraPlatform();
    NativeCameraPlatform.instance = fakePlatform;

    expect(await nativeCameraPlugin.getPlatformVersion(), '42');
  });
}
