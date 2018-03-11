# Multistage docker build, requires docker 17.05

# builder stage
FROM ubuntu:16.04 as builder

RUN apt-get update && \
    apt-get --no-install-recommends --yes install \
        ca-certificates \
        cmake \
        g++ \
        libboost1.58-all-dev \
        libssl-dev \
        libzmq-dev \
        make \
        pkg-config \
        graphviz \
        doxygen \
        git

WORKDIR /src
COPY . .
RUN rm -rf build && \
    qmake -j$(nproc) release-static

# runtime stage
FROM ubuntu:16.04

RUN apt-get update && \
    apt-get --no-install-recommends --yes install \
        ca-certificates \
        libboost1.58-all \
        libssl1.0.0 \
        libzmq1 && \
    apt-get clean && \
    rm -rf /var/lib/apt

COPY --from=builder /src/build/release/bin/* /usr/local/bin/

# Contains the blockchain
VOLUME /root/.bitmonero

# Generate your wallet via accessing the container and run:
# cd /wallet
# monero-wallet-cli
VOLUME /wallet

EXPOSE 18080
EXPOSE 18081

ENTRYPOINT ["monerod", "--p2p-bind-ip=0.0.0.0", "--p2p-bind-port=18080", "--rpc-bind-ip=127.0.0.1", "--rpc-bind-port=18081", "--non-interactive"] 
