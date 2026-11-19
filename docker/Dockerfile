FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive

# archive.ubuntu.com is unusably slow from the dev host; override with
# --build-arg UBUNTU_MIRROR=<url> elsewhere
ARG UBUNTU_MIRROR=http://tw.archive.ubuntu.com/ubuntu
RUN sed -i -E "s#http://(archive|security)\.ubuntu\.com/ubuntu#${UBUNTU_MIRROR}#" \
		/etc/apt/sources.list

RUN apt-get update && \
	apt-get install -y \
		build-essential \
		ninja-build \
		meson \
		zlib1g-dev \
		libjpeg-dev \
		cmake \
		libtiff-dev \
		libopenjp2-7-dev \
		libpng-dev \
		libxml2-dev \
		libcairo2-dev \
		libgdk-pixbuf-2.0-dev \
		libzstd-dev \
		libjpeg-turbo8-dev \
		doxygen \
		libsqlite3-dev \
		libjpeg-turbo-progs \
		xdelta3 \
		python3-venv

# pre-commit and its hook dependencies (pyfuse3 builds against fuse3).
# Run with -e PRE_COMMIT_HOME=/mnt/build/pre-commit to keep hook
# environments across containers.
RUN apt-get update && \
	apt-get install -y \
		git \
		libfuse3-dev \
		pkg-config \
		python3-dev && \
	python3 -m venv /opt/pre-commit && \
	/opt/pre-commit/bin/pip install pre-commit==4.2.0 && \
	ln -s /opt/pre-commit/bin/pre-commit /usr/local/bin/pre-commit && \
	git config --system --add safe.directory '*'
