FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install software-properties-common to get add-apt-repository
RUN apt-get update && apt-get install -y software-properties-common \
  && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
  && apt-get update && apt-get install -y \
  build-essential \
  g++-13 \
  gcc-13 \
  cmake \
  git \
  libgtest-dev \
  libcurl4-openssl-dev \
  libssl-dev \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set GCC 13 / G++ 13 as the default compilers
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 13 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-13

WORKDIR /workspace
CMD ["/bin/bash"]
