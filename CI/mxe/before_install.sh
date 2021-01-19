#!/bin/sh

# Install nsis for installer creation
sudo apt-get install -qq nsis ninja-build

if true; then
	# MXE repository was too slow for Travis far too often
	wget https://github.com/nullkiller/vcmi-deps-mxe/releases/download/2021-01-22/mxe-$MXE_TARGET-2021-01-22.tar
	tar -xvf mxe-$MXE_TARGET-2021-01-22.tar
	sudo dpkg -i mxe-*.deb
	sudo apt-get install -f --yes

else
	# Add MXE repository and key
	echo "deb http://pkg.mxe.cc/repos/apt/debian wheezy main" \
		| sudo tee /etc/apt/sources.list.d/mxeapt.list

	sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys D43A795B73B16ABE9643FE1AFD8FFF16DB45C6AB

	# Install needed packages
	sudo apt-get update -qq

	sudo apt-get install -q --yes \
	mxe-$MXE_TARGET-gcc \
	mxe-$MXE_TARGET-boost \
	mxe-$MXE_TARGET-zlib \
	mxe-$MXE_TARGET-sdl2 \
	mxe-$MXE_TARGET-sdl2-gfx \
	mxe-$MXE_TARGET-sdl2-image \
	mxe-$MXE_TARGET-sdl2-mixer \
	mxe-$MXE_TARGET-sdl2-ttf \
	mxe-$MXE_TARGET-ffmpeg \
	mxe-$MXE_TARGET-qt \
	mxe-$MXE_TARGET-qtbase \
	mxe-$MXE_TARGET-intel-tbb

fi # Disable

# alias for CMake
sudo mv /usr/bin/cmake /usr/bin/cmake.orig
sudo ln -s /usr/lib/mxe/usr/bin/$MXE_TARGET-cmake /usr/bin/cmake
