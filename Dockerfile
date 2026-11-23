# Build environment: docker build -t rosc-build . (see README)
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make && rm -rf /var/lib/apt/lists/*
