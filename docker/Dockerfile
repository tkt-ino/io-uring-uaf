FROM rust:1.78.0

RUN apt update && apt install -y libcap-dev

RUN mkdir -p /tmp/liburing \
    && wget -O - https://github.com/axboe/liburing/archive/refs/tags/liburing-2.5.tar.gz \
    | tar xzf - -C /tmp/liburing --strip-components 1

WORKDIR /tmp/liburing
RUN make -j$(nproc) && make install

WORKDIR /workdir
ENTRYPOINT ["make"]
