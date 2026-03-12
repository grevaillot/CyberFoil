#include "ui/idle_backlight.hpp"

namespace inst::ui {

    static constexpr u64 kIdleDimSeconds = 15;
    static constexpr float kDimBrightness = 0.0f;

    static u64 lastInputTick = 0;
    static bool dimmed = false;
    static float savedBrightness = -1.0f;

    static void restoreBrightness() {
        if (!dimmed)
            return;
        if (R_SUCCEEDED(lblInitialize())) {
            if (savedBrightness >= 0.0f)
                lblSetCurrentBrightnessSetting(savedBrightness);
            lblExit();
        }
        dimmed = false;
    }

    void IdleBacklight::Update() {
        if (dimmed)
            return;
        const u64 now = armGetSystemTick();
        const u64 freq = armGetSystemTickFreq();
        if (lastInputTick == 0) {
            lastInputTick = now;
            return;
        }
        if ((now - lastInputTick) / freq < kIdleDimSeconds)
            return;
        if (R_SUCCEEDED(lblInitialize())) {
            float current = -1.0f;
            if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&current)) && current > kDimBrightness) {
                savedBrightness = current;
                lblSetCurrentBrightnessSetting(kDimBrightness);
                dimmed = true;
            }
            lblExit();
        }
    }

    void IdleBacklight::ResetTimer() {
        lastInputTick = armGetSystemTick();
        restoreBrightness();
    }

    void IdleBacklight::Restore() {
        restoreBrightness();
    }

}
