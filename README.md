**This repo has submodules**. Clone with `--recursive`, or do `git
submodule init` then `git submodule update` after cloning.

Prerequisites:

* Mac laptop (mine has Ventura 13.7.8)
* Xcode (I used `15.2 (15c500b)`)
* cmake (I used `3.31.2`)

# Xcode

To build, do this from the root of the working copy:

	rm -R build
	cmake -G Xcode -B Build
	
Load `build/sdl2_macos_audio_sleep.xcodeproj` into Xcode.

Set `sdl2_macos_audio_sleep` as the scheme.

Build and run.


