#!/bin/bash

# Exit on error
set -e

echo "Building Xbox ISO Extractor..."

# Build native library
echo "Building native library..."
cd src/native
./build.sh
cd ../..

# Get Flutter dependencies
echo "Getting Flutter dependencies..."
flutter pub get

# Build and run Flutter app
echo "Building and running Flutter app..."
flutter run -d linux
