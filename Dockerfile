FROM ubuntu:xenial
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt-get update -y && apt-get install -y \
	wget \
	vim \
	time \
	git \
	autogen \
	libtool \
	ccache \
	exuberant-ctags \
	autoconf \
	make \
	gdb \
	cmake \
	libedit-dev \
	libbsd-dev \
	build-essential \
	libsodium-dev \
	clang-4.0 \
	lldb-4.0 \
	libstdc++6 \
	libz-dev

# Make sure llvm-link and clang are linked to be available without version numbers
RUN update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-4.0 100 \
	&& update-alternatives --install /usr/bin/clang clang /usr/bin/clang-4.0 100 \
	&& update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-4.0 100 \
	&& update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-4.0 100 \
	&& update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-4.0 100

RUN \
	mkdir -p /tmp/jansson && \
	cd /tmp/jansson && \
	git clone https://github.com/akheron/jansson /tmp/jansson && \
	autoreconf -i && \
	./configure && \
	make && \
	make install

ENV ARC4RANDOM_LIB bsd
ADD . /opt/zion
WORKDIR /opt/zion
CMD bash
