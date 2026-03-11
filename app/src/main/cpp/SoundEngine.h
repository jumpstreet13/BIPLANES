#ifndef BIPLANES_SOUNDENGINE_H
#define BIPLANES_SOUNDENGINE_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/asset_manager.h>
#include <vector>

class SoundEngine {
public:
    void init(AAssetManager *assetManager);
    void playExplosion();
    void playShoot();
    void playHit();
    void playVictory();
    void shutdown();

private:
    struct SoundSlot {
        SLObjectItf playerObj = nullptr;
        SLPlayItf player = nullptr;
        SLAndroidSimpleBufferQueueItf bufferQueue = nullptr;
        std::vector<short> pcm;
        int sampleRate = 22050;
    };

    bool loadWav(AAssetManager *mgr, const char *filename, SoundSlot &slot);
    bool createPlayer(SoundSlot &slot);

    SLObjectItf engineObj_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLObjectItf outputMixObj_ = nullptr;

    SoundSlot explosion_;
    SoundSlot shoot_;
    SoundSlot hit_;
    SoundSlot victory_;

    bool initialized_ = false;
};

#endif
