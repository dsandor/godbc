#!/bin/bash

# Exit on error
set -e

echo "Building Docker image..."
docker build -t godbc-darwin-arm64 -f Dockerfile.darwin-arm64 .

echo "Creating temporary container..."
CONTAINER_ID=$(docker create --platform linux/arm64 godbc-darwin-arm64)

echo "Copying release archive from container..."
docker cp $CONTAINER_ID:/build/darwin-arm64-Release.tar.gz .

echo "Removing temporary container..."
docker rm $CONTAINER_ID

echo "Darwin ARM64 release has been built and extracted to darwin-arm64-Release.tar.gz" 