#ifndef ENG_I_SOUND_EFFECT_ENGINE_HPP
#define ENG_I_SOUND_EFFECT_ENGINE_HPP

#include "eng/Common.hpp"

namespace engine {

/**
 * @brief Interface for synthesized navigation audio effects.
 */
class ISoundEffectEngine {
public:
    virtual ~ISoundEffectEngine() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void playSound(SoundEffect effect) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
};

} // namespace engine

#endif // ENG_I_SOUND_EFFECT_ENGINE_HPP
