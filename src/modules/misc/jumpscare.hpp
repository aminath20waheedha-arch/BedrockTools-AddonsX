#pragma once

#include "../Module.hpp"
#include <atomic>
#include <chrono>

class JumpscareModule : public Module {
public:
    JumpscareModule();
    ~JumpscareModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;

    // called from the tick hook every frame with the current jump-key state
    std::atomic<bool> bSpace{false};

private:
    bool m_wasJumping = false;
    bool m_scareActive = false;
    std::chrono::steady_clock::time_point m_scareStart;
    float m_scareDurationMs = 600.0f; // how long the flash stays on screen
};
