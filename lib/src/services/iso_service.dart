import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:path/path.dart' as path;
import 'package:ffi/ffi.dart';

// FFI type definitions
typedef XisoInitFunc = ffi.Bool Function();
typedef XisoCleanupFunc = ffi.Void Function();
typedef XisoExtractFunc = ffi.Bool Function(
    ffi.Pointer<ffi.Char> isoPath, ffi.Pointer<ffi.Char> outputPath);
typedef XisoListFunc = ffi.Bool Function(
    ffi.Pointer<ffi.Char> isoPath, ffi.Pointer<ffi.Char> buffer, ffi.Size bufferSize);
typedef XisoGetLastErrorFunc = ffi.Pointer<ffi.Char> Function();

// Dart function signatures
typedef XisoInit = bool Function();
typedef XisoCleanup = void Function();
typedef XisoExtract = bool Function(
    ffi.Pointer<ffi.Char> isoPath, ffi.Pointer<ffi.Char> outputPath);
typedef XisoList = bool Function(
    ffi.Pointer<ffi.Char> isoPath, ffi.Pointer<ffi.Char> buffer, int bufferSize);
typedef XisoGetLastError = ffi.Pointer<ffi.Char> Function();

class IsoService {
  static late final ffi.DynamicLibrary _lib;
  static late final XisoInit _xisoInit;
  static late final XisoCleanup _xisoCleanup;
  static late final XisoExtract _xisoExtract;
  static late final XisoList _xisoList;
  static late final XisoGetLastError _xisoGetLastError;
  static bool _initialized = false;

  static void _initLibrary() {
    if (_initialized) return;

    final libraryPath = _getLibraryPath();
    _lib = ffi.DynamicLibrary.open(libraryPath);

    _xisoInit = _lib
        .lookupFunction<XisoInitFunc, XisoInit>('xiso_init');
    _xisoCleanup = _lib
        .lookupFunction<XisoCleanupFunc, XisoCleanup>('xiso_cleanup');
    _xisoExtract = _lib
        .lookupFunction<XisoExtractFunc, XisoExtract>('xiso_extract');
    _xisoList = _lib
        .lookupFunction<XisoListFunc, XisoList>('xiso_list');
    _xisoGetLastError = _lib
        .lookupFunction<XisoGetLastErrorFunc, XisoGetLastError>('xiso_get_last_error');

    _initialized = true;
  }

  static String _getLibraryPath() {
    final libraryName = Platform.isWindows
        ? 'xiso.dll'
        : Platform.isMacOS
            ? 'libxiso.dylib'
            : 'libxiso.so';

    // During development, the library is in the native/build/lib directory
    final devPath = path.join(
      Directory.current.path,
      'src',
      'native',
      'build',
      'lib',
      libraryName,
    );

    if (File(devPath).existsSync()) {
      return devPath;
    }

    // In production, the library should be next to the executable
    return path.join(
      Directory.current.path,
      libraryName,
    );
  }

  static String _getLastError() {
    final errorPtr = _xisoGetLastError();
    return errorPtr.cast<Utf8>().toDartString();
  }

  static Future<String> listContents(String isoPath) async {
    _initLibrary();
    
    // Initialize XISO library
    if (!_xisoInit()) {
      throw Exception('Failed to initialize XISO library: ${_getLastError()}');
    }

    try {
      // Allocate a large buffer for the listing
      final bufferSize = 1024 * 1024; // 1MB should be enough
      final buffer = calloc<ffi.Char>(bufferSize);
      final isoPathPtr = isoPath.toNativeUtf8();

      try {
        final result = _xisoList(isoPathPtr.cast(), buffer, bufferSize);
        if (!result) {
          throw Exception('Failed to list ISO contents: ${_getLastError()}');
        }

        return buffer.cast<Utf8>().toDartString();
      } finally {
        calloc.free(buffer);
        calloc.free(isoPathPtr);
      }
    } finally {
      _xisoCleanup();
    }
  }

  static Future<void> extractContents(String isoPath, String outputPath) async {
    _initLibrary();
    
    // Initialize XISO library
    if (!_xisoInit()) {
      throw Exception('Failed to initialize XISO library: ${_getLastError()}');
    }

    try {
      final isoPathPtr = isoPath.toNativeUtf8();
      final outputPathPtr = outputPath.toNativeUtf8();

      try {
        final result = _xisoExtract(isoPathPtr.cast(), outputPathPtr.cast());
        if (!result) {
          throw Exception('Failed to extract ISO: ${_getLastError()}');
        }
      } finally {
        calloc.free(isoPathPtr);
        calloc.free(outputPathPtr);
      }
    } finally {
      _xisoCleanup();
    }
  }
}
