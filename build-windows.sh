#!/bin/bash

# Exit on error
set -e

echo "Building Docker image..."
docker build -t godbc-windows -f Dockerfile.windows .

echo "Creating temporary container..."
CONTAINER_ID=$(docker create --platform linux/amd64 godbc-windows)

echo "Copying release archive from container..."
docker cp $CONTAINER_ID:/build/windows-msvc-Release.zip .

echo "Removing temporary container..."
docker rm $CONTAINER_ID

echo "Windows release has been built and extracted to windows-msvc-Release.zip" 