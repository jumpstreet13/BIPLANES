#include "SoundEngine.h"
#include "AndroidOut.h"
#include <android/asset_manager.h>
#include <cstring>

bool SoundEngine::loadWav(AAssetManager *mgr, const char *filename, SoundSlot &slot) {
    AAsset *asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    if (!asset) {
        aout << "SoundEngine: failed to open " << filename << std::endl;
        return false;
    }

    off_t length = AAsset_getLength(asset);
    if (length < 44) {
        AAsset_close(asset);
        return false;
    }

    const unsigned char *data = (const unsigned char *)AAsset_getBuffer(asset);

    // Parse WAV header (assumes 16-bit mono PCM)
    memcpy(&slot.sampleRate, data + 24, 4);
    int dataSize = (int)(length - 44);
    int numSamples = dataSize / 2;

    slot.pcm.resize(numSamples);
    memcpy(slot.pcm.data(), data + 44, dataSize);

    AAsset_close(asset);
    aout << "SoundEngine: loaded " << filename << " (" << slot.sampleRate << "Hz, "
         << numSamples << " samples)" << std::endl;
    return true;
}

bool SoundEngine::createPlayer(SoundSlot &slot) {
    SLDataLocator_AndroidSimpleBufferQueue locBufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };
    SLDataFormat_PCM formatPcm = {
        SL_DATAFORMAT_PCM,
        1,
        (SLuint32)(slot.sampleRate * 1000),
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_CENTER,
        SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource audioSrc = {&locBufq, &formatPcm};

    SLDataLocator_OutputMix locOutmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObj_};
    SLDataSink audioSnk = {&locOutmix, nullptr};

    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[] = {SL_BOOLEAN_TRUE};

    SLresult result;
    result = (*engine_)->CreateAudioPlayer(engine_, &slot.playerObj, &audioSrc, &audioSnk, 1, ids, req);
    if (result != SL_RESULT_SUCCESS) return false;

    result = (*slot.playerObj)->Realize(slot.playerObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return false;

    (*slot.playerObj)->GetInterface(slot.playerObj, SL_IID_PLAY, &slot.player);
    (*slot.playerObj)->GetInterface(slot.playerObj, SL_IID_BUFFERQUEUE, &slot.bufferQueue);
    (*slot.player)->SetPlayState(slot.player, SL_PLAYSTATE_PLAYING);

    return true;
}

void SoundEngine::init(AAssetManager *assetManager) {
    if (initialized_) return;

    // Load all WAV files
    bool ok = loadWav(assetManager, "explosion_sound.wav", explosion_);
    ok = loadWav(assetManager, "shoot_sound.wav", shoot_) && ok;
    ok = loadWav(assetManager, "hit_plane_sound.wav", hit_) && ok;
    ok = loadWav(assetManager, "victory_sound.wav", victory_) && ok;

    if (!ok) {
        aout << "SoundEngine: warning - some sounds failed to load" << std::endl;
    }

    // Create OpenSL ES engine
    SLresult result;
    result = slCreateEngine(&engineObj_, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return;

    result = (*engineObj_)->Realize(engineObj_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return;

    result = (*engineObj_)->GetInterface(engineObj_, SL_IID_ENGINE, &engine_);
    if (result != SL_RESULT_SUCCESS) return;

    // Create output mix
    result = (*engine_)->CreateOutputMix(engine_, &outputMixObj_, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return;

    result = (*outputMixObj_)->Realize(outputMixObj_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return;

    // Create a player for each sound
    if (!explosion_.pcm.empty()) createPlayer(explosion_);
    if (!shoot_.pcm.empty()) createPlayer(shoot_);
    if (!hit_.pcm.empty()) createPlayer(hit_);
    if (!victory_.pcm.empty()) createPlayer(victory_);

    initialized_ = true;
    aout << "SoundEngine: initialized OK" << std::endl;
}

void SoundEngine::playExplosion() {
    if (!initialized_ || !explosion_.bufferQueue || explosion_.pcm.empty()) return;
    (*explosion_.bufferQueue)->Clear(explosion_.bufferQueue);
    (*explosion_.bufferQueue)->Enqueue(explosion_.bufferQueue,
                                       explosion_.pcm.data(),
                                       explosion_.pcm.size() * sizeof(short));
}

void SoundEngine::playShoot() {
    if (!initialized_ || !shoot_.bufferQueue || shoot_.pcm.empty()) return;
    (*shoot_.bufferQueue)->Clear(shoot_.bufferQueue);
    (*shoot_.bufferQueue)->Enqueue(shoot_.bufferQueue,
                                    shoot_.pcm.data(),
                                    shoot_.pcm.size() * sizeof(short));
}

void SoundEngine::playHit() {
    if (!initialized_ || !hit_.bufferQueue || hit_.pcm.empty()) return;
    (*hit_.bufferQueue)->Clear(hit_.bufferQueue);
    (*hit_.bufferQueue)->Enqueue(hit_.bufferQueue,
                                  hit_.pcm.data(),
                                  hit_.pcm.size() * sizeof(short));
}

void SoundEngine::playVictory() {
    if (!initialized_ || !victory_.bufferQueue || victory_.pcm.empty()) return;
    (*victory_.bufferQueue)->Clear(victory_.bufferQueue);
    (*victory_.bufferQueue)->Enqueue(victory_.bufferQueue,
                                      victory_.pcm.data(),
                                      victory_.pcm.size() * sizeof(short));
}

void SoundEngine::shutdown() {
    SoundSlot *slots[] = {&explosion_, &shoot_, &hit_, &victory_};
    for (auto *s : slots) {
        if (s->playerObj) {
            (*s->playerObj)->Destroy(s->playerObj);
            s->playerObj = nullptr;
            s->player = nullptr;
            s->bufferQueue = nullptr;
        }
    }
    if (outputMixObj_) {
        (*outputMixObj_)->Destroy(outputMixObj_);
        outputMixObj_ = nullptr;
    }
    if (engineObj_) {
        (*engineObj_)->Destroy(engineObj_);
        engineObj_ = nullptr;
        engine_ = nullptr;
    }
    initialized_ = false;
}
