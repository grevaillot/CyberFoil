#pragma once
#include <switch.h>

namespace inst::ui {

    // Reusable idle-backlight dimmer.
    //
    // Usage from any page:
    //   Constructor:  this->AddThread(IdleBacklight::Update);
    //   onInput:      if (Down || !Pos.IsEmpty()) IdleBacklight::ResetTimer();
    //   Exit paths:   IdleBacklight::Restore();
    namespace IdleBacklight {
        // Call every frame (register with AddThread). Dims after idle timeout.
        void Update();
        // Call on any user input to reset the idle countdown and restore brightness.
        void ResetTimer();
        // Call before leaving a page or exiting the app to restore brightness.
        void Restore();
    }

}
