import 'package:flutter/material.dart';
import 'package:file_picker/file_picker.dart';
import 'package:path/path.dart' as path;
import '../services/iso_service.dart';

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  String? selectedIsoPath;
  String? outputPath;
  String status = '';
  bool isProcessing = false;

  Future<void> _pickIsoFile() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['iso'],
    );

    if (result != null) {
      setState(() {
        selectedIsoPath = result.files.single.path;
      });
    }
  }

  Future<void> _pickOutputDirectory() async {
    final result = await FilePicker.platform.getDirectoryPath();
    if (result != null) {
      setState(() {
        outputPath = result;
      });
    }
  }

  Future<void> _listContents() async {
    if (selectedIsoPath == null) {
      setState(() {
        status = 'Please select an ISO file first';
      });
      return;
    }

    setState(() {
      isProcessing = true;
      status = 'Listing contents...';
    });

    try {
      final contents = await IsoService.listContents(selectedIsoPath!);
      setState(() {
        status = contents;
      });
    } catch (e) {
      setState(() {
        status = 'Error: ${e.toString()}';
      });
    } finally {
      setState(() {
        isProcessing = false;
      });
    }
  }

  Future<void> _extractContents() async {
    if (selectedIsoPath == null) {
      setState(() {
        status = 'Please select an ISO file first';
      });
      return;
    }

    if (outputPath == null) {
      setState(() {
        status = 'Please select an output directory first';
      });
      return;
    }

    setState(() {
      isProcessing = true;
      status = 'Extracting...';
    });

    try {
      await IsoService.extractContents(selectedIsoPath!, outputPath!);
      setState(() {
        status = 'Extraction completed successfully';
      });
    } catch (e) {
      setState(() {
        status = 'Error: ${e.toString()}';
      });
    } finally {
      setState(() {
        isProcessing = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Xbox ISO Extractor'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // ISO File Selection
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'ISO File',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: Text(
                            selectedIsoPath ?? 'No file selected',
                            style: TextStyle(
                              color: selectedIsoPath == null ? Colors.grey : null,
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        ElevatedButton(
                          onPressed: _pickIsoFile,
                          child: const Text('Browse'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // Output Directory Selection
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Output Directory',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: Text(
                            outputPath ?? 'No directory selected',
                            style: TextStyle(
                              color: outputPath == null ? Colors.grey : null,
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        ElevatedButton(
                          onPressed: _pickOutputDirectory,
                          child: const Text('Browse'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // Action Buttons
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                Expanded(
                  child: ElevatedButton(
                    onPressed: isProcessing ? null : _listContents,
                    child: const Text('List Contents'),
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: ElevatedButton(
                    onPressed: isProcessing ? null : _extractContents,
                    child: const Text('Extract All'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),

            // Status Display
            Expanded(
              child: Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Status',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 8),
                      if (isProcessing)
                        const LinearProgressIndicator()
                      else
                        Expanded(
                          child: SingleChildScrollView(
                            child: Text(status),
                          ),
                        ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
