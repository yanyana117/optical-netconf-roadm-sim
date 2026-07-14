# ARM/x86 Linux network element: Netopeer2 NETCONF server + sysrepo datastore
# + the onsim simulated optical device behind its C HAL.
#
# The CESNET NETCONF stack (libyang / sysrepo / libnetconf2 / netopeer2) is
# built from source at pinned, mutually compatible release tags -- the same
# way real network elements ship it (distro packaging is spotty).
FROM debian:trixie-slim

RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        build-essential cmake pkg-config git \
        libssl-dev libssh-dev libpcre2-dev libcurl4-openssl-dev \
        python3 python3-ncclient openssh-client procps \
    && rm -rf /var/lib/apt/lists/*

# --- CESNET NETCONF stack, pinned versions ---
ARG LIBYANG_VER=v3.7.8
ARG SYSREPO_VER=v3.3.10
ARG LIBNETCONF2_VER=v3.5.1
ARG NETOPEER2_VER=v2.2.31

RUN git clone --depth 1 -b ${LIBYANG_VER} https://github.com/CESNET/libyang.git /tmp/libyang \
    && cmake -S /tmp/libyang -B /tmp/libyang/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/libyang/build -j2 && cmake --install /tmp/libyang/build \
    && ldconfig && rm -rf /tmp/libyang

RUN git clone --depth 1 -b ${SYSREPO_VER} https://github.com/sysrepo/sysrepo.git /tmp/sysrepo \
    && cmake -S /tmp/sysrepo -B /tmp/sysrepo/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/sysrepo/build -j2 && cmake --install /tmp/sysrepo/build \
    && ldconfig && rm -rf /tmp/sysrepo

RUN git clone --depth 1 -b ${LIBNETCONF2_VER} https://github.com/CESNET/libnetconf2.git /tmp/libnetconf2 \
    && cmake -S /tmp/libnetconf2 -B /tmp/libnetconf2/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/libnetconf2/build -j2 && cmake --install /tmp/libnetconf2/build \
    && ldconfig && rm -rf /tmp/libnetconf2

RUN git clone --depth 1 -b ${NETOPEER2_VER} https://github.com/CESNET/netopeer2.git /tmp/netopeer2 \
    && cmake -S /tmp/netopeer2 -B /tmp/netopeer2/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/netopeer2/build -j2 && cmake --install /tmp/netopeer2/build \
    && ldconfig && rm -rf /tmp/netopeer2

# --- the simulated device + its management daemon ---
WORKDIR /onsim
COPY CMakeLists.txt LICENSE ./
COPY include include
COPY src src
COPY netconf netconf
COPY yang yang
COPY docker/entrypoint.sh docker/demo_netconf.py docker/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DONSIM_NETCONF=ON -DONSIM_BUILD_TESTS=OFF \
    && cmake --build build -j2 \
    && cp build/onsim-netconfd /usr/local/bin/

EXPOSE 830
ENTRYPOINT ["/onsim/docker/entrypoint.sh"]
