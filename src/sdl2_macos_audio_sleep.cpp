#include <stdio.h>
#include <SDL.h>
#include <mach/mach_port.h>
#include <mach/mach_interface.h>
#include <mach/mach_init.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/IOMessage.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <assert.h>
#include <string>

// https://developer.apple.com/library/archive/qa/qa1340/_index.html

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

enum Behaviour {
    // Do nothing on wake/sleep.
    Behaviour_None,
    
    // Pause audio device on sleep, unpause on wake.
    Behaviour_PauseDevice,
    
    // Reopen audio device on wake.
    Behaviour_ReopenAudioDevice,
};

static constexpr Behaviour g_behaviour=Behaviour_PauseDevice;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// (some copy and pasted fluff from my shared code stuff)

static volatile uint32_t g_got_timebase_metrics;
static double g_timebase_secs_per_tick;

uint64_t GetCurrentTickCount(void) {
    return mach_absolute_time();
}

double GetSecondsPerTick(void) {
    if (!g_got_timebase_metrics) {
        /* It doesn't matter if there's a race here; all the threads
         * will (should...) get the same value. */

        mach_timebase_info_data_t tbi;
        mach_timebase_info(&tbi);

        // Intel: 1/1 = 1.0000
        // M: 125/3 = 41.6667
        double ns_per_tick = (double)tbi.numer / tbi.denom;
        g_timebase_secs_per_tick = ns_per_tick / 1e9;

        g_got_timebase_metrics = 1;
    }

    return g_timebase_secs_per_tick;
}

double GetSecondsFromTicks(uint64_t ticks) {
    return ticks * GetSecondsPerTick();
}

#define GetMillisecondsFromTicks(TICKS) (GetSecondsFromTicks(TICKS) * 1000.)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static UInt32 g_power_on_event;
static SDL_AudioDeviceID g_audio_device_id = 0;

static void SleepCallback(void *refCon, io_service_t service, natural_t message, void *message_arg) {
    (void)refCon, (void)service, (void)message_arg;

    if (message == kIOMessageSystemWillSleep) {
        printf("SleepCallback: kIOMessageSystemWillSleep (now=%.4f s)\n", GetSecondsFromTicks(GetCurrentTickCount()));

        if(g_behaviour==Behaviour_PauseDevice){
            printf("Behaviour_PauseDevice: pausing device\n");
            SDL_PauseAudioDevice(g_audio_device_id, 1);
        }
    } else if (message == kIOMessageSystemHasPoweredOn) {
        printf("SleepCallback: kIOMessageSystemHasPoweredOn (now=%.4f s)\n", GetSecondsFromTicks(GetCurrentTickCount()));
        
        if(g_behaviour==Behaviour_ReopenAudioDevice){
            SDL_Event event;
            event.type = g_power_on_event;
            
            SDL_PushEvent(&event);
        }else if(g_behaviour==Behaviour_PauseDevice){
            printf("Behaviour_PauseDevice: unpausing device\n");
            SDL_PauseAudioDevice(g_audio_device_id,0);
        }
    }
}

static io_connect_t g_root_port;
static IONotificationPortRef g_notify_port_ref;
static io_object_t g_notifier;

static void AddSleepCallback() {
    g_root_port = IORegisterForSystemPower(nullptr, &g_notify_port_ref, &SleepCallback, &g_notifier);
    if(g_root_port==0){
        fprintf(stderr,"FATAL: IORegisterForSystemPower failed\n");
        exit(1);
    }

    CFRunLoopAddSource(CFRunLoopGetCurrent(),
                       IONotificationPortGetRunLoopSource(g_notify_port_ref), kCFRunLoopCommonModes);
}

static void RemoveSleepCallback() {
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(),
                          IONotificationPortGetRunLoopSource(g_notify_port_ref),
                          kCFRunLoopCommonModes);

    IODeregisterForSystemPower(&g_notifier), g_notifier = 0;

    IOServiceClose(g_root_port), g_root_port = 0;

    IONotificationPortDestroy(g_notify_port_ref), g_notify_port_ref = nullptr;
}

static Uint16*g_wav_buf;
static Uint32 g_wav_len;
static Uint32 g_wav_index=0;

struct AudioCallbackState {
    uint64_t last_callback_ticks = 0;
    uint64_t num_calls = 0;
};

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream_, int len) {
    assert(len >= 0);
    auto s = (AudioCallbackState *)userdata;

    uint64_t now_ticks = GetCurrentTickCount();

    printf("%" PRIu64 ": now=%.4f s; dt=%.3f ms\n", s->num_calls, GetSecondsFromTicks(now_ticks), GetMillisecondsFromTicks(now_ticks - s->last_callback_ticks));

    s->last_callback_ticks = now_ticks;
    ++s->num_calls;
    
    assert(len%2==0);
    auto stream=(Uint16*)stream_;
    for(int i=0;i<len/2;++i){
        stream[i]=g_wav_buf[g_wav_index];
        
        ++g_wav_index;
        g_wav_index%=g_wav_len;
    }
}

static AudioCallbackState g_audio_callback_state;

static void CloseAudioDevice() {
    if (g_audio_device_id != 0) {
        printf("closing audio device: %" PRIu32 "\n", g_audio_device_id);
        SDL_PauseAudioDevice(g_audio_device_id, 1);
        SDL_CloseAudioDevice(g_audio_device_id);
        g_audio_device_id = 0;
    }
}

static void ReopenAudioDevice() {
    printf("ReopenAudioDevice...\n");

    if (g_audio_device_id != 0) {
        CloseAudioDevice();
    }

    g_audio_callback_state = {};

    // format chosen to match the WAV file
    SDL_AudioSpec desired_spec = {};
    desired_spec.freq = 48000;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.samples = 1024;
    desired_spec.callback = &AudioCallback;
    desired_spec.userdata = &g_audio_callback_state;

    SDL_AudioSpec obtained_spec = {};

    g_audio_device_id = SDL_OpenAudioDevice(nullptr,
                                            0, //playback
                                            &desired_spec,
                                            &obtained_spec,
                                            0);

    printf("device_id=%d\n", g_audio_device_id);
    
    if(g_audio_device_id==0){
        fprintf(stderr,"FATAL: SDL_OpenAudioDevice failed: %s\n",SDL_GetError());
        exit(1);
    }

    printf("obtained spec: freq=%d\n", obtained_spec.freq);
    printf("               format=%d (BITSIZE=%d ISFLOAT=%d BE=%d SIGNED=%d)\n", obtained_spec.format, SDL_AUDIO_BITSIZE(obtained_spec.format), !!SDL_AUDIO_ISFLOAT(obtained_spec.format), !!SDL_AUDIO_ISBIGENDIAN(obtained_spec.format), !!SDL_AUDIO_ISSIGNED(obtained_spec.format));
    printf("               channels=%" PRIu8 "\n", obtained_spec.channels);
    printf("               samples=%" PRIu16 "\n", obtained_spec.samples);

    SDL_PauseAudioDevice(g_audio_device_id, 0);
}

int main(int argc, char *argv[]) {
    (void)argc, (void)argv;

    if(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_AUDIO)!=0){
        fprintf(stderr,"FATAL: SDL_Init failed: %s\n",SDL_GetError());
        return 1;
    }

    g_power_on_event = SDL_RegisterEvents(1);
    
    std::string wav_path=ASSETS_DIR;
    if(!wav_path.empty()){
        if(wav_path.back()!='/'){
            wav_path+="/";
        }
    }
    
    wav_path+="cusb_ed_80073_01_1252_0ad.wav";
    
    SDL_AudioSpec wav_spec;
    Uint8*wav_buf;
    Uint32 wav_len;
    if(!SDL_LoadWAV(wav_path.c_str(),&wav_spec,&wav_buf,&wav_len)){
        fprintf(stderr,"FATAL: failed to load \"%s\": %s\n",wav_path.c_str(),SDL_GetError());
        return 1;
    }
    assert(wav_len%2==0);
    g_wav_buf=(Uint16*)wav_buf;
    g_wav_len=wav_len/2;
    
    SDL_Window *w = SDL_CreateWindow("audio test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 250, 250, 0);
    if(!w){
        fprintf(stderr,"FATAL: SDL_CreateWindow failed: %s\n",SDL_GetError());
        return 1;
    }

    // 0=query sound playback devices
    for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
        const char *name = SDL_GetAudioDeviceName(i, 0);
        printf("Device %d: %s\n", i, name);
    }

    AddSleepCallback();

    ReopenAudioDevice();

    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            goto done;

        default:
            if (ev.type == g_power_on_event) {
                ReopenAudioDevice();
            }
            break;
        }
    }

done:
    
    RemoveSleepCallback();

    CloseAudioDevice();

    SDL_Quit();
}
