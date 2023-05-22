# docker --build -t criu-dev . \
#     --network host --build-arg=http_proxy=http://127.0.0.1:7890 \
#     --build-arg=https_proxy=http://127.0.0.1:7890

# docker run --rm -v $CRIU_PATH:/criu --name criu-build "crui-dev" \
#   bash -c "cd /criu && make clean && make -j"
#

ARG GO_VERSION=1.19.8
ARG BASE_DEBIAN_DISTRO="bullseye"
ARG GOLANG_IMAGE="golang:${GO_VERSION}-${BASE_DEBIAN_DISTRO}"

FROM --platform=$BUILDPLATFORM ${GOLANG_IMAGE}
ARG DEBIAN_FRONTEND
ADD --chmod=0644 https://download.opensuse.org/repositories/devel:/tools:/criu/Debian_11/Release.key /etc/apt/trusted.gpg.d/criu.gpg.asc
RUN --mount=type=cache,sharing=locked,id=moby-criu-aptlib,target=/var/lib/apt \
    --mount=type=cache,sharing=locked,id=moby-criu-aptcache,target=/var/cache/apt \
    --mount=type=bind,target=/criu \
        echo 'deb https://download.opensuse.org/repositories/devel:/tools:/criu/Debian_11/ /' > /etc/apt/sources.list.d/criu.list \
        && apt-get update \
        && apt-get install -y --no-install-recommends criu libprotobuf-dev libprotobuf-c-dev \
            protobuf-c-compiler protobuf-compiler python3-protobuf \
            libnl-3-dev libnet1-dev libcap-dev iproute2