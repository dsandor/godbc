#!/bin/bash

# Exit on error
set -e

echo "Building Docker image..."
docker build -t godbc-darwin -f Dockerfile.darwin .

echo "Creating temporary container..."
CONTAINER_ID=$(docker create --name temp_godbc_darwin godbc-darwin)

echo "Copying release archive from container..."
docker cp temp_godbc_darwin:/build/darwin-Release.zip .

echo "Removing temporary container..."
docker rm temp_godbc_darwin

echo "Darwin release has been built and extracted to darwin-Release.zip" 