FROM chimbuko/mochi:ubuntu18.04 AS build_base

COPY --from=chimbuko/adios2:ubuntu18.04 /opt /opt

#################
FROM build_base AS build

COPY . /Downloads/

WORKDIR /Downloads/PerformanceAnalysis
RUN ./autogen.sh

WORKDIR /build

SHELL ["/bin/bash", "-c"]

ENV CPATH=/opt/cereal/include:${CPATH}

RUN source /spack/spack/share/spack/setup-env.sh && spack load boost mochi-sonata && \
    CC=mpicc CXX=mpicxx ../Downloads/PerformanceAnalysis/configure --with-adios2=/opt/adios2 --with-network=ZMQ --with-perf-metric --prefix=/opt/chimbuko/ad --enable-gcov --with-pkg-config && \
    make -j 4 install && cd /opt/chimbuko/ad/test

RUN cd /Downloads/PerformanceAnalysis && git log | head -1 > /opt/chimbuko/ad/git_version

RUN apt-get update && apt-get install -y lcov vim

COPY run_tests_upload_coverage.sh /Downloads/

WORKDIR /Downloads

RUN chmod a+x run_tests_upload_coverage.sh

ENTRYPOINT [ "./run_tests_upload_coverage.sh" ]
