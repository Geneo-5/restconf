# ==============================================================================
# RESTCONF Server - Docker Buildfile (RFC 8040 + RFC 8527)
# ==============================================================================
# Build and runtime container for restconfd
# Dependencies: fcgi2, libyang (sysrepo), pthreads

FROM debian:bookworm-slim

ARG DEBIAN_FRONTEND=noninteractive

# -----------------------------------------------------------------------------
# Compilation tools
# -----------------------------------------------------------------------------
RUN apt-get update && apt-get install -y build-essential cmake gcc make \
    m4 autoconf automake libtool libssl-dev libjson-c-dev libpcre2-dev nginx \
    && rm -rf /var/lib/apt/lists/*

ADD https://github.com/FastCGI-Archives/fcgi2.git /fcgi2
ADD https://github.com/sysrepo/sysrepo.git /sysrepo
ADD https://github.com/CESNET/libyang.git /libyang

# -----------------------------------------------------------------------------
# Build stage
# -----------------------------------------------------------------------------
WORKDIR /fcgi2
RUN ./autogen.sh
RUN ./configure --prefix=/usr
RUN make
RUN make install

WORKDIR /libyang/build
RUN cmake .. -DCMAKE_INSTALL_PREFIX=/usr
RUN make
RUN make install

WORKDIR /sysrepo/build
RUN cmake .. -DCMAKE_INSTALL_PREFIX=/usr
RUN make
RUN make install

WORKDIR /restconf/build

COPY . ..

# Build the RESTCONF server (using CMake properly)
RUN cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
RUN make
RUN make install

RUN sysrepoctl -i /restconf/yang/ietf-restconf@2017-01-26.yang
RUN sysrepoctl -i /restconf/yang/ietf-restconf-monitoring@2017-01-26.yang
RUN sysrepoctl -i /restconf/yang/ietf-restconf-subscribed-notifications@2019-11-17.yang

RUN nginx -t -c /restconf/etc/nginx-restconf.conf.example

CMD ["/restconf/docker/start-nginx.sh"]
