#!/bin/bash

set -e

echo "Building Docker image..."
docker build -t godbc-rhel7 -f Dockerfile.rhel7 .

echo "Creating temporary container..."
container_id=$(docker create godbc-rhel7)

if [ -z "$container_id" ]; then
    echo "Failed to create container"
    exit 1
fi

echo "Copying release archive from container..."
docker cp $container_id:/build/rhel7-gcc-Release.tar.gz .

echo "Removing temporary container..."
docker rm $container_id

if [ -f rhel7-gcc-Release.tar.gz ]; then
    echo "RHEL 7 release has been built and extracted to rhel7-gcc-Release.tar.gz"
else
    echo "Failed to create release archive"
    exit 1
fi 