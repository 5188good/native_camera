import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:native_camera/native_camera.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('native_camera');

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  group('getCameras', () {
    test('returns parsed list', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async {
        if (call.method == 'getCameras') {
          return ['Camera 1', 'Camera 2'];
        }
        return null;
      });

      final result = await NativeCamera.getCameras();
      expect(result, ['Camera 1', 'Camera 2']);
    });

    test('returns empty list on null', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => null);

      final result = await NativeCamera.getCameras();
      expect(result, isEmpty);
    });
  });

  group('openCamera', () {
    test('returns null on success', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => '');

      final result = await NativeCamera.openCamera(0);
      expect(result, isNull);
    });

    test('returns error on PlatformException', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async {
        throw PlatformException(code: 'ERR', message: 'Not found');
      });

      final result = await NativeCamera.openCamera(0);
      expect(result, 'Not found');
    });
  });

  group('getResolutions', () {
    test('returns parsed list', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async {
        if (call.method == 'getResolutions') {
          return [
            {'width': 1280, 'height': 720},
            {'width': 640, 'height': 480},
          ];
        }
        return null;
      });

      final result = await NativeCamera.getResolutions(0);
      expect(result.length, 2);
      expect(result[0].width, 1280);
      expect(result[0].height, 720);
    });

    test('returns empty on non-list result', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => 'not a list');

      final result = await NativeCamera.getResolutions(0);
      expect(result, isEmpty);
    });
  });

  group('setResolution', () {
    test('returns null on success', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => null);

      final result = await NativeCamera.setResolution(640, 480);
      expect(result, isNull);
    });
  });

  group('switchCamera', () {
    test('returns null on success', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => null);

      final result = await NativeCamera.switchCamera(1);
      expect(result, isNull);
    });
  });

  group('setFps', () {
    test('returns result from native', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async => 30);

      final result = await NativeCamera.setFps(30);
      expect(result, 30);
    });
  });

  group('CameraResolution', () {
    test('toString formats correctly', () {
      final res = CameraResolution(width: 1920, height: 1080);
      expect(res.toString(), '1920x1080');
    });
  });

  group('CameraFrame', () {
    test('creates with all fields', () {
      final bytes = Uint8List.fromList([1, 2, 3]);
      final frame = CameraFrame(width: 640, height: 480, rgbBytes: bytes);
      expect(frame.width, 640);
      expect(frame.height, 480);
      expect(frame.rgbBytes, bytes);
    });
  });
}
