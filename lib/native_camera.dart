import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/services.dart';

/// 原生摄像头插件
/// 通过 C++ Media Foundation 获取 Windows 摄像头帧数据
class NativeCamera {
  static const _channel = MethodChannel('native_camera');
  static const _frameChannel = EventChannel('native_camera/frames');

  StreamSubscription? _frameSub;
  StreamController<CameraFrame> _frameController = StreamController<CameraFrame>.broadcast();

  /// 获取摄像头列表
  static Future<List<String>> getCameras() async {
    final list = await _channel.invokeListMethod<String>('getCameras');
    return list ?? [];
  }

  /// 获取摄像头支持的分辨率列表
  static Future<List<CameraResolution>> getResolutions(int cameraIndex) async {
    final list = await _channel.invokeMethod('getResolutions', {
      'cameraIndex': cameraIndex,
    });
    if (list is List) {
      return list
          .map((e) => CameraResolution(
                width: (e as Map)['width'] as int,
                height: e['height'] as int,
              ))
          .toList();
    }
    return [];
  }

  /// 设置摄像头分辨率（需在 openCamera 之后、startStream 之前调用）
  static Future<String?> setResolution(int width, int height) async {
    try {
      await _channel.invokeMethod('setResolution', {
        'width': width,
        'height': height,
      });
      return null;
    } on PlatformException catch (e) {
      return e.message ?? 'Unknown error';
    }
  }

  /// 设置帧率（1-60，需在 startStream 之前调用）
  static Future<int> setFps(int fps) async {
    final result = await _channel.invokeMethod('setFps', {'fps': fps});
    return result as int;
  }

  /// 打开摄像头，成功返回 null，失败返回错误描述
  static Future<String?> openCamera(int index) async {
    try {
      await _channel.invokeMethod('openCamera', {'index': index});
      return null;
    } on PlatformException catch (e) {
      return e.message ?? 'Unknown error';
    }
  }

  /// 运行时切换摄像头
  static Future<String?> switchCamera(int index) async {
    try {
      await _channel.invokeMethod('switchCamera', {'index': index});
      return null;
    } on PlatformException catch (e) {
      return e.message ?? 'Unknown error';
    }
  }

  /// 开始推送帧数据
  Stream<CameraFrame> startStream() {
    // Cancel any existing subscription and close old controller
    _frameSub?.cancel();
    _frameSub = null;
    if (!_frameController.isClosed) {
      _frameController.close();
    }
    // Create fresh controller for this stream session
    _frameController = StreamController<CameraFrame>.broadcast();

    _channel.invokeMethod('startStream');
    _frameSub = _frameChannel.receiveBroadcastStream().listen((data) {
      if (data is Map) {
        final width = data['width'] as int;
        final height = data['height'] as int;
        final bytes = data['bytes'] as List<int>;
        _frameController.add(CameraFrame(
          width: width,
          height: height,
          rgbBytes: Uint8List.fromList(bytes),
        ));
      }
    });
    return _frameController.stream;
  }

  /// 停止推送
  Future<void> stopStream() async {
    await _frameSub?.cancel();
    _frameSub = null;
    await _channel.invokeMethod('stopStream');
  }

  /// 关闭摄像头
  Future<void> closeCamera() async {
    await stopStream();
    await _channel.invokeMethod('closeCamera');
  }

  void dispose() {
    _frameController.close();
  }
}

/// 摄像头分辨率
class CameraResolution {
  final int width;
  final int height;

  CameraResolution({required this.width, required this.height});

  @override
  String toString() => '${width}x$height';
}

/// 摄像头帧数据
class CameraFrame {
  final int width;
  final int height;
  final Uint8List rgbBytes;

  CameraFrame({
    required this.width,
    required this.height,
    required this.rgbBytes,
  });
}
