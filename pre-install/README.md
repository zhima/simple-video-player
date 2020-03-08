Simple-Video-Player
===============
## Introduction

Instructions of installing `ffmpeg` library and `SDL2` library, so we can make successful.

## ffmpeg
1. git clone --depth=1 https://git.ffmpeg.org/ffmpeg.git ffmpeg //下载最新版 ffmpeg 库
2. cd ffmpeg
3. ./configure --disable-videotoolbox --disable-securetransport --disable-audiotoolbox
4. make -j4 //4线程编译加快速度
5. sudo make install

下载 ffmpeg 库(https://www.ffmpeg.org/download.html)
git clone --depth=1 https://git.ffmpeg.org/ffmpeg.git ffmpeg
由于我只需要最新的代码，不需要历史版本，所以加了个 --depth=1 选项加快下载速度，减少下载流量。
下载后进入 ffmpeg 目录进行编译安装，但是由于 ffmpeg 库默认是没有禁止依赖特定系统的配置的，所以如果想编译出各个系统通用的库，需要在 configure 阶段进行配置。通过 ./configure --help 可以查看所有配置选项。

在make 的过程中可能会报错，需要 nasm/yasm。

还有一种办法是下载旧版本 2.x.x的 ffmpeg 库，这样就可以直接编译安装.
ffmpeg 的各个大版本是独立维护的，也就是 2.x.x 和 4.x.x 都是各自进行代码和版本更新的，主要是为了兼容性。
目前代码中使用的是 4.2.2 版本的 ffmpeg 库。

## SDL2

下载SDL2库(https://www.libsdl.org/download-2.0.php)
下载源码压缩包，不要下 dmg 的二进制包，这个里面是一个SDL2.framework，需要自己将其拖到 /Library/Frameworks 里面，而且在 makefile 里的链接是用 -lSDL2，这个是链接动态库 dylib 的方式，如果需要链接 framework 要用 -framework SDL2，但这种方式一是需要指定 framework 的链接目录 -L/Library/Frameworks，二是不能在各系统通用，只能在Mac OS X上使用，因为是只有 OS X 的 gcc 才支持这种链接方式。

下载 SDL2 源码后用这些命令进行编译安装

	mkdir build
	cd build
	CC=/where/i/cloned/SDL/build-scripts/gcc-fat.sh ../configure
	make
	


















