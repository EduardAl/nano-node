FROM nanocurrency/vban-env:gcc

ARG NETWORK=live
ARG TRAVIS_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
cd /tmp/build && \
cmake /tmp/src -DCI_BUILD=${CI_BUILD} \
-DBOOST_ROOT=${BOOST_ROOT} -DPORTABLE=1 \
-DACTIVE_NETWORK=vban_${NETWORK}_network \
-DVBAN_POW_SERVER=ON -DVBAN_SHARED_BOOST=ON && \
make vban_node -j $(nproc) && \
make vban_rpc -j $(nproc) && \
make vban_pow_server -j $(nproc) && \
cd .. && \
echo ${NETWORK} >/etc/vban-network

FROM ubuntu:18.04

RUN groupadd --gid 1000 nanocurrency && \
useradd --uid 1000 --gid nanocurrency --shell /bin/bash --create-home nanocurrency

COPY --from=0 /tmp/build/vban_node /usr/bin
COPY --from=0 /tmp/build/vban_rpc /usr/bin
COPY --from=0 /tmp/build/vban_pow_server /usr/bin
COPY --from=0 /tmp/src/api/ /usr/bin/api/
COPY --from=0 /etc/vban-network /etc
COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/vban/config
COPY --from=0 /tmp/boost/lib/* /usr/local/lib/
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/vban_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/usr/bin/entry.sh"]
CMD ["vban_node", "daemon", "-l"]
ARG REPOSITORY=nanocurrency/vban-node
LABEL org.opencontainers.image.source https://github.com/$REPOSITORY
