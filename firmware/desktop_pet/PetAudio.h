// PetAudio.h — non-blocking sound engine for the desktop pet.
// Output: I2S data pin only (GPIO3 / RX) as a 2nd-order 1-bit delta-sigma stream at 32 kHz x 32
// oversampling; the board's RC filter turns it into analog for the LM386. GPIO15 (OLED CS) and
// GPIO2 are never touched. Call PetAudio::service() often (every loop pass): the DMA buffer holds
// only 16 ms of audio.
#ifndef _PETAUDIO_h
#define _PETAUDIO_h

#include <Arduino.h>

enum SoundId : uint8_t {
  SND_NONE = 0,
  SND_BOOT,      // rising "waking up" chirp
  SND_HAPPY,     // two quick rising chirps (touch)
  SND_PURR,      // looping AM purr while held; ends via PetAudio::release()
  SND_BLIP,      // quiet content blip
  SND_CURIOUS,   // "hm?" rising question chirp
  SND_WHINE,     // descending whine (neglected)
  SND_PETME,     // SAM "pet me"
  SND_MUTE,      // falling two-tone (plays even though muting)
  SND_UNMUTE,    // rising two-tone
  SND_COUNT
};

namespace PetAudio {
  void begin();                  // start I2S on GPIO3, ramp up silently to the idle level
  void service();                // keep the DMA buffer full; cheap when idle
  bool play(SoundId id);         // start a sound; returns false if blocked by mute or priority
  void release();                // let a looping sound (purr) fade out
  void stop();                   // cut the current sound (with a short de-click fade)
  bool busy();                   // a sound is playing
  SoundId current();
  void setMuted(bool m);
  bool muted();
  uint32_t takeUnderflows();     // underflows since last call (should stay 0)
}

#endif
