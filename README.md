Investigate possible macOS bug noticed when sleeping/waking Mac
laptops.

**This repo has submodules**. Clone with `--recursive`, or do `git
submodule init` then `git submodule update` after cloning.

Prerequisites:

* Mac laptop (mine has Ventura 13.7.8)
* Xcode (I used `15.2 (15c500b)`)
* cmake (I used `3.31.2`)
* GNU Make (the one that comes with Xcode will do)

# Building with Xcode

To set up, do this from the root of the working copy:

	rm -R build
	cmake -G Xcode -B build
	
Load `build/sdl2_macos_audio_sleep.xcodeproj` into Xcode.

Set `sdl2_macos_audio_sleep` as the scheme.

Build and run.

# Building with GNU Make

To set up, do this from the root of the working copy:

	rm -R build2
    cmake -G "Unix Makefiles" -B build2
	
To build:

    cd build2
	make -j `sysctl -n hw.ncpu`
	
To run:

	./sdl2_macos_audio_sleep

# Process

The program opens a window and plays a WAV file (crackly
copyright-expired recording of Chopin's funeral march). It uses the
SDL audio device callback to do this; on each call, it fills the
buffer with as many samples from the WAV data as are required. When
the end of the WAV data is reached, it loops back to the start. (It's
not very complicated! See the code.)

The sleep/wake-related behaviour is edited by modifying the source.
The thing to tweak is `g_behaviour`, near the top:

- `Behaviour_None`: nothing done on sleep or wake
- `Behaviour_ReopenAudioDevice`: nothing done on sleep; on wake, close
  the audio device and re-open it
- `Behaviour_PauseDevice`: on sleep, pause audio device; on wake,
  unpause audio device

To stop it, quit it.

To see the issue:

1. sleep the Mac with `Apple` > `Sleep`

2. Wait for a minute or so

3. Wake the Mac up and log back in

Expected results: nothing surprising. The Mac goes to sleep, and when
it wakes up the program carries on where it left off.

Actual results:

On Mac Studio, behaves as expected. (Tested on M4 Max.)

On Macbook Pro, behaviour depends on `g_behaviour`. (Tested on mid
2015 13" Macbook Pro running Ventura 13.7.8 with OCLP. I was moved to
look into this after getting related reports from Intel and Apple
Silicon laptop users, various models, both with and without OCLP.)

- `Behaviour_None`: playbacks stops on sleep. On wake, the callback is
  called much more often than expected, and no audio output, for a
  period that seems to relate to the sleep period - then eventually
  normal behaviour resumes
- `Behaviour_PauseDevice`: not obviously different from `BehaviourNone`
- `Behaviour_ReopenAudioDevice`: playback stops on sleep. On wake,
  playback resumes immediately

# Other notes

Audio from here:
https://adp.library.ucsb.edu/index.php/matrix/detail/2000150135 -
recorded 1912, so out of copyright
