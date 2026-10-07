FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

# Install software-properties-common to get add-apt-repository
RUN apt-get update && apt-get install -y software-properties-common \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libgtest-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    unzip \
    netcat-openbsd \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN gcc --version
RUN git --version

# For prebuilding libraries
RUN mkdir -p /jiaolong_external/source
RUN mkdir -p /jiaolong_external/prebuilt

# Prebuild library cpr
RUN git clone --no-checkout https://github.com/libcpr/cpr.git /jiaolong_external/source/cpr
RUN cd /jiaolong_external/source/cpr && git fetch --depth=1 origin f091b2c061b307ee89b164c39976fc9202a1c79d
RUN cd /jiaolong_external/source/cpr && git checkout f091b2c061b307ee89b164c39976fc9202a1c79d
RUN rm -rf /jiaolong_external/source/cpr/.git
RUN mkdir -p /jiaolong_external/source/cpr/build
RUN cd /jiaolong_external/source/cpr/build && cmake .. -DCMAKE_INSTALL_PREFIX=/jiaolong_external/prebuilt/cpr -DCPR_BUILD_TESTS=OFF -DCPR_USE_SYSTEM_CURL=ON -DBUILD_SHARED_LIBS=OFF
RUN cd /jiaolong_external/source/cpr/build && cmake --build . -j 4
RUN cd /jiaolong_external/source/cpr/build && cmake --install .

# Prebuild library nlohmann json
RUN git clone --no-checkout https://github.com/nlohmann/json.git /jiaolong_external/source/nlohmann_json
RUN cd /jiaolong_external/source/nlohmann_json && git fetch --depth=1 origin 55f93686c01528224f448c19128836e7df245f72
RUN cd /jiaolong_external/source/nlohmann_json && git checkout 55f93686c01528224f448c19128836e7df245f72
RUN rm -rf /jiaolong_external/source/nlohmann_json/.git
RUN mkdir -p /jiaolong_external/source/nlohmann_json/build
RUN cd /jiaolong_external/source/nlohmann_json/build && cmake .. -DCMAKE_INSTALL_PREFIX=/jiaolong_external/prebuilt/nlohmann_json
RUN cd /jiaolong_external/source/nlohmann_json/build && cmake --build . -j 4
RUN cd /jiaolong_external/source/nlohmann_json/build && cmake --install .

# Prebuild library stduuid
RUN git clone --no-checkout https://github.com/mariusbancila/stduuid.git /jiaolong_external/source/stduuid
RUN cd /jiaolong_external/source/stduuid && git fetch --depth=1 origin 3afe7193facd5d674de709fccc44d5055e144d7a
RUN cd /jiaolong_external/source/stduuid && git checkout 3afe7193facd5d674de709fccc44d5055e144d7a
RUN rm -rf /jiaolong_external/source/stduuid/.git
RUN mkdir -p /jiaolong_external/source/stduuid/build
RUN cd /jiaolong_external/source/stduuid/build && cmake .. -DCMAKE_INSTALL_PREFIX=/jiaolong_external/prebuilt/stduuid
RUN cd /jiaolong_external/source/stduuid/build && cmake --build . -j 4
RUN cd /jiaolong_external/source/stduuid/build && cmake --install .

# Prebuild library ftxui
RUN git clone --no-checkout https://github.com/ArthurSonzogni/FTXUI.git /jiaolong_external/source/ftxui
RUN cd /jiaolong_external/source/ftxui && git fetch --depth=1 origin f921fad208912747c17d129a8ef75ec7624b6eec
RUN cd /jiaolong_external/source/ftxui && git checkout f921fad208912747c17d129a8ef75ec7624b6eec
RUN rm -rf /jiaolong_external/source/ftxui/.git
RUN mkdir -p /jiaolong_external/source/ftxui/build
RUN cd /jiaolong_external/source/ftxui/build && cmake .. -DCMAKE_INSTALL_PREFIX=/jiaolong_external/prebuilt/ftxui
RUN cd /jiaolong_external/source/ftxui/build && cmake --build . -j 4
RUN cd /jiaolong_external/source/ftxui/build && cmake --install .

# Prebuilt (download) library httplib
RUN git clone --no-checkout https://github.com/yhirose/cpp-httplib.git /jiaolong_external/source/httplib
RUN cd /jiaolong_external/source/httplib && git fetch --depth=1 origin 9d6a7ee2c1aaeb1fd9ae15d14f06f487149d147f
RUN cd /jiaolong_external/source/httplib && git checkout 9d6a7ee2c1aaeb1fd9ae15d14f06f487149d147f
RUN rm -rf /jiaolong_external/source/httplib/.git
RUN mkdir -p /jiaolong_external/prebuilt/httplib/include
RUN cp /jiaolong_external/source/httplib/httplib.h /jiaolong_external/prebuilt/httplib/include/

# Library SQLite
COPY sqlite/sqlite-amalgamation-3530400.zip /
RUN unzip /sqlite-amalgamation-3530400.zip -d /jiaolong_external/prebuilt
RUN mv /jiaolong_external/prebuilt/sqlite-amalgamation-3530400 /jiaolong_external/prebuilt/sqlite

# JJ (Jujutsu) version control
RUN mkdir -p /root/.local/bin/jj/v0.43.0
COPY jj/v0.43.0/jj /root/.local/bin/jj/v0.43.0

# Set up Git identity
RUN git config --global user.name "user"
RUN git config --global user.email "user@example.com"

# Set up JJ identity
RUN /root/.local/bin/jj/v0.43.0/jj config set --user user.name "user"
RUN /root/.local/bin/jj/v0.43.0/jj config set --user user.email "user@example.com"

# GitHub CLI
RUN mkdir -p /root/.local/bin/gh_2.100.0
COPY gh_2.100.0/gh /root/.local/bin/gh_2.100.0

WORKDIR /workspace
CMD ["/bin/bash"]
