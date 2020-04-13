#!/usr/bin/env bash

#set -x

function extract()
{
     if [ -f "$1" ] ; then
         case "$1" in
             *.tar.bz2)   tar xvjf "$1"     ;;
             *.tar.gz)    tar xvzf "$1"     ;;
             *.bz2)       bunzip2 "$1"      ;;
             *.rar)       unrar x "$1"      ;;
             *.gz)        gunzip "$1"       ;;
             *.tar)       tar xvf "$1"      ;;
             *.tbz2)      tar xvjf "$1"     ;;
             *.tgz)       tar xvzf "$1"     ;;
             *.zip)       unzip "$1"        ;;
             *.Z)         uncompress "$1"   ;;
             *.7z)        7z x "$1"         ;;
             *)           echo "$1 cannot be extracted via >extract<" ;;
         esac
     else
         echo "'$1' is not a valid file"
     fi
}

MPG123_VERSION="1.25.13"
MPG123_EXTENSION=".tar.bz2"
MPG123_DIRECTORY="mpg123-${MPG123_VERSION}"
MPG123_TARBALL="mpg123-${MPG123_VERSION}${MPG123_EXTENSION}"

# Only download tarball again if not already downloaded
if [[ ! -f "${MPG123_TARBALL}" ]]; then
  wget -v -nc "https://sourceforge.net/projects/mpg123/files/mpg123/${MPG123_VERSION}/${MPG123_TARBALL}"
fi

# Only extract tarball again if not already extracted
if [[ ! -d "$MPG123_DIRECTORY" ]]; then
  extract "$MPG123_TARBALL"
fi

# Ensure ANDROID_NDK is set
# if [[ ! -z "$ANDROID_NDK" ]]; then
NDK_VERSION="19.2.5345600"
export ANDROID_NDK="/home/osboxes/Android/Sdk/ndk/${NDK_VERSION}"
#fi

if [[ ! -d "$MPG123_DIRECTORY" ]];
then
  echo "Problem with extracting $MPG123_TARBALL into $MPG123_DIRECTORY!!!"
  exit -1
fi

# Move to extracted directory
cd "$MPG123_DIRECTORY"

# LLVM Toolchain
ARCH_ABI="llvm"
HOST_TAG="linux-x86_64"
TARGET="aarch64-linux-android"
MIN_SDK="28"
echo "ANDROID_NDK=${ANDROID_NDK}"
[[ ! -d "$ANDROID_NDK" ]] && echo "ANDROID_NDK not correct" && exit -1

export AR="$ANDROID_NDK/toolchains/${ARCH_ABI}/prebuilt/${HOST_TAG}/bin/${TARGET}-ar"
echo "AR=${AR}"
[[ ! -f "$AR" ]] && echo "AR not correct" && exit -1

export LD="$ANDROID_NDK/toolchains/${ARCH_ABI}/prebuilt/${HOST_TAG}/bin/${TARGET}-ld"
echo "LD=${LD}"
[[ ! -f "$LD" ]] && echo "LD not correct" && exit -1

export CC="$ANDROID_NDK/toolchains/${ARCH_ABI}/prebuilt/${HOST_TAG}/bin/${TARGET}${MIN_SDK}-clang"
echo "CC=${CC}"
[[ ! -f "$CC" ]] && echo "CC not correct" && exit -1

export CXX="$ANDROID_NDK/toolchains/${ARCH_ABI}/prebuilt/${HOST_TAG}/bin/${TARGET}${MIN_SDK}-clang++"
[[ ! -f "$CXX" ]] && echo "CXX not correct" && exit -1

ANDROID_PLATFORM="android-${MIN_SDK}"
echo "ANDROID_PLATFORM=${ANDROID_PLATFORM}"
SYSROOT="${ANDROID_NDK}/platforms/${ANDROID_PLATFORM}/arch-arm64"
echo "SYSROOT=${SYSROOT}"
[[ ! -f "${SYSROOT}/usr/lib/crtbegin_dynamic.o" ]] && echo "SYSROOT not correct" && exit -1

# Configure build
#export HWKIND="yellowstone"
#export CPPFLAGS="--sysroot=$ANDROID_NDK/platforms/${ANDROID_PLATFORM}/arch-arm -DANDROID_HARDWARE_$HWKIND"
#export CPPFLAGS=""
#export CFLAGS="--sysroot=${SYSROOT}"
#export LDFLAGS="-Wl,--exclude-libs,libgcc.a -lc -ldl"
#export LIBRARY_PATH="${SYSROOT}/usr/lib"

BUILD=true

if [[ "$BUILD" = true ]];
then
  ##################### Configure #######################
  
  ./configure \
  --host ${TARGET}
#  --build ${HOST_TAG}  # don't activate this on Linux on Windows!!

  [[ $? -ne 0 ]] && echo "Can't configure!" && exit -1

  # Determine the number of jobs (commands) to be run simultaneously by GNU Make
  NO_CPU_CORES=$(grep -c ^processor /proc/cpuinfo)

  if [ $NO_CPU_CORES -le 8 ]; then
    JOBS=$(($NO_CPU_CORES+1))
  else
    JOBS=${NO_CPU_CORES}
  fi

  ##################### Compile #######################
  
  make -j "${JOBS}"

  [[ $? -ne 0 ]] && echo "Can't compile!" && exit -1

  # Install
  ##################### Install #######################
  
  make -j "${JOBS}" install DESTDIR="$(pwd)/Inst"
  [[ $? -ne 0 ]] && echo "Can't install!" && exit -1
fi

exit
