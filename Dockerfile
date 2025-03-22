FROM golang:1.21-alpine AS go-builder

# Install build dependencies
RUN apk add --no-cache gcc musl-dev

# Set working directory
WORKDIR /build

# Copy Go files
COPY bridge/bridge.go .
COPY bridge/bridge.h .
COPY bridge/go.mod .
COPY bridge/go.sum .

# Build Go bridge
RUN go build -buildmode=c-shared -o libgodbc.so bridge.go

# Create C++ build stage
FROM alpine:latest AS cpp-builder

# Install build dependencies
RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    go

# Set working directory
WORKDIR /build

# Copy Go bridge files from previous stage
COPY --from=go-builder /build/libgodbc.so /build/
COPY --from=go-builder /build/bridge.h /build/

# Copy C++ files
COPY cpp/ /build/cpp/
COPY examples/ /build/examples/
COPY CMakeLists.txt /build/

# Build C++ code
RUN rm -rf build && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_STANDARD=17 \
          -DCMAKE_CXX_STANDARD_REQUIRED=ON \
          -DCMAKE_PREFIX_PATH=/build .. && \
    make -j$(nproc)

# Create final stage
FROM alpine:latest

# Install runtime dependencies
RUN apk add --no-cache \
    libstdc++ \
    libgcc \
    freetds-dev \
    unixodbc-dev

# Copy built files from C++ builder
COPY --from=cpp-builder /build/libgodbc.so /usr/lib/
COPY --from=cpp-builder /build/build/bin/benchmark /usr/local/bin/
COPY --from=cpp-builder /build/build/bin/example /usr/local/bin/
COPY --from=cpp-builder /build/build/bin/query_examples /usr/local/bin/

# Set environment variables
ENV LD_LIBRARY_PATH=/usr/lib
ENV ODBCINI=/etc/odbc.ini
ENV ODBCSYSINI=/etc

# Create ODBC configuration directory
RUN mkdir -p /etc

# Copy ODBC configuration
COPY odbc.ini /etc/odbc.ini

# Set working directory
WORKDIR /app

# Command to run the benchmark
ENTRYPOINT ["benchmark"] 