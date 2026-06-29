FROM gcr.io/oss-fuzz-base/base-builder

RUN apt-get update && apt-get install -y \
    make \
    autoconf \
    automake \
    libtool \
    pkg-config \
    zip

COPY . $SRC/viewbrowserhistory
WORKDIR $SRC/viewbrowserhistory

COPY .clusterfuzzlite/build.sh $SRC/build.sh
