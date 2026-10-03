#ifndef ENG_I_SCREEN_HPP
#define ENG_I_SCREEN_HPP

#include "eng/Common.hpp"
#include "eng/interfaces/IStatefulFeature.hpp"

namespace engine {

/**
 * @brief Base interface for all UI screen presenters.
 *
 * Every UI screen represents a concrete feature in the engine and is contractually
 * required to implement IStatefulFeature to govern its states via an IStateMachine.
 */
class IScreen : public IStatefulFeature {
public:
    virtual ~IScreen() = default;

    virtual ScreenId getScreenId() const = 0;
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual bool handleInput(uint32_t pressed, uint32_t held, uint32_t released) = 0;
    virtual void update(double deltaMs) = 0;
    virtual void render(uint32_t* framebuffer, int width, int height) = 0;
};

} // namespace engine

#endif // ENG_I_SCREEN_HPP
