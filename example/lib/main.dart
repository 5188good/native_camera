import 'package:flutter/material.dart';
import 'dart:async';
import 'dart:ui' as ui;
import 'package:native_camera/native_camera.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final _camera = NativeCamera();
  List<String> _cameras = [];
  List<CameraResolution> _resolutions = [];
  CameraResolution? _currentResolution;
  int _selectedCamera = 0;
  int? _activeCamera;
  bool _isStreaming = false;
  ui.Image? _currentFrame;
  String _errorMsg = '';

  @override
  void initState() {
    super.initState();
    _loadCameras();
  }

  Future<void> _loadCameras() async {
    try {
      final cameras = await NativeCamera.getCameras();
      setState(() => _cameras = cameras);
    } catch (e) {
      setState(() => _errorMsg = 'Enum error: $e');
    }
  }

  Future<void> _loadResolutions(int cameraIndex) async {
    try {
      final resolutions = await NativeCamera.getResolutions(cameraIndex);
      setState(() {
        _resolutions = resolutions;
        if (resolutions.isNotEmpty) {
          _currentResolution = resolutions.last; // default: 640x480 last? No, smallest first
          _currentResolution = resolutions.firstWhere(
            (r) => r.width <= 640 && r.height <= 480,
            orElse: () => resolutions.first,
          );
        }
      });
    } catch (e) {
      setState(() => _errorMsg = 'Resolution error: $e');
    }
  }

  Future<void> _startCamera(int index) async {
    setState(() => _errorMsg = '');
    try {
      final err = await NativeCamera.openCamera(index);
      if (err != null) {
        setState(() => _errorMsg = err);
        return;
      }
      _activeCamera = index;
      await _loadResolutions(index);

      // Apply default resolution
      if (_currentResolution != null) {
        await NativeCamera.setResolution(
            _currentResolution!.width, _currentResolution!.height);
      }

      final stream = _camera.startStream();
      setState(() => _isStreaming = true);

      stream.listen((frame) {
        try {
          ui.decodeImageFromPixels(
            frame.rgbBytes,
            frame.width,
            frame.height,
            ui.PixelFormat.bgra8888,
            (image) {
              if (mounted) setState(() => _currentFrame = image);
            },
          );
        } catch (e) {
          setState(() => _errorMsg = 'Decode error: $e');
        }
      }, onError: (e) {
        setState(() => _errorMsg = 'Stream error: $e');
      });
    } catch (e) {
      setState(() => _errorMsg = 'Open error: $e');
    }
  }

  Future<void> _switchCamera(int index) async {
    if (index == _activeCamera) return;
    setState(() => _errorMsg = '');
    final err = await NativeCamera.switchCamera(index);
    if (err != null) {
      setState(() => _errorMsg = err);
      return;
    }
    _activeCamera = index;
    await _loadResolutions(index);
    if (_currentResolution != null) {
      await NativeCamera.setResolution(
          _currentResolution!.width, _currentResolution!.height);
    }
  }

  Future<void> _changeResolution(CameraResolution res) async {
    if (!_isStreaming) return;
    // Stop stream, change resolution, restart
    await _camera.stopStream();
    final err = await NativeCamera.setResolution(res.width, res.height);
    if (err != null) {
      setState(() => _errorMsg = err);
      return;
    }
    setState(() => _currentResolution = res);

    // Restart stream
    final stream = _camera.startStream();
    stream.listen((frame) {
      try {
        ui.decodeImageFromPixels(
          frame.rgbBytes,
          frame.width,
          frame.height,
          ui.PixelFormat.bgra8888,
          (image) {
            if (mounted) setState(() => _currentFrame = image);
          },
        );
      } catch (e) {
        setState(() => _errorMsg = 'Decode error: $e');
      }
    }, onError: (e) {
      setState(() => _errorMsg = 'Stream error: $e');
    });
  }

  @override
  void dispose() {
    _camera.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('Native Camera Test')),
        body: Column(
          children: [
            if (_errorMsg.isNotEmpty)
              Container(
                width: double.infinity,
                color: Colors.red.shade100,
                padding: const EdgeInsets.all(8),
                child: Text(_errorMsg,
                    style: TextStyle(color: Colors.red.shade900)),
              ),
            // Camera preview
            if (_currentFrame != null)
              Expanded(
                child: CustomPaint(
                  painter: _ImagePainter(_currentFrame!),
                  size: Size.infinite,
                ),
              ),
            // Controls
            if (_isStreaming) ...[
              if (_resolutions.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: Row(
                    children: [
                      const Text('Resolution: '),
                      Expanded(
                        child: DropdownButton<CameraResolution>(
                          value: _currentResolution,
                          isExpanded: true,
                          items: _resolutions.map((r) {
                            return DropdownMenuItem(
                              value: r,
                              child: Text(r.toString()),
                            );
                          }).toList(),
                          onChanged: (r) {
                            if (r != null) _changeResolution(r);
                          },
                        ),
                      ),
                    ],
                  ),
                ),
              if (_cameras.length > 1)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                  child: Row(
                    children: [
                      const Text('Camera: '),
                      Expanded(
                        child: DropdownButton<int>(
                          value: _activeCamera,
                          isExpanded: true,
                          items: _cameras.asMap().entries.map((e) {
                            return DropdownMenuItem(
                              value: e.key,
                              child: Text(e.value,
                                  overflow: TextOverflow.ellipsis),
                            );
                          }).toList(),
                          onChanged: (idx) {
                            if (idx != null) _switchCamera(idx);
                          },
                        ),
                      ),
                    ],
                  ),
                ),
              ElevatedButton(
                onPressed: () {
                  _camera.closeCamera();
                  setState(() {
                    _isStreaming = false;
                    _currentFrame = null;
                    _activeCamera = null;
                    _resolutions = [];
                    _currentResolution = null;
                  });
                },
                child: const Text('Stop'),
              ),
            ],
            // Camera list (when not streaming)
            if (!_isStreaming) ...[
              if (_cameras.isEmpty)
                const Padding(
                  padding: EdgeInsets.all(20),
                  child: Text('No cameras found'),
                )
              else
                ..._cameras.asMap().entries.map((e) => ListTile(
                      title: Text(e.value),
                      trailing: ElevatedButton(
                        onPressed: () => _startCamera(e.key),
                        child: const Text('Open'),
                      ),
                    )),
            ],
          ],
        ),
      ),
    );
  }
}

class _ImagePainter extends CustomPainter {
  final ui.Image image;
  _ImagePainter(this.image);

  @override
  void paint(Canvas canvas, Size size) {
    final scaleX = size.width / image.width;
    final scaleY = size.height / image.height;
    final scale = scaleX < scaleY ? scaleX : scaleY;
    final dst = Rect.fromLTWH(0, 0, image.width * scale, image.height * scale);
    canvas.drawImageRect(
      image,
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      dst,
      Paint(),
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}
