export class TriggerHapticController {
  // =========================
  // States
  // =========================
  static State = Object.freeze({
    IDLE: "IDLE",
    PROFILE_LOW: "PROFILE_LOW",
    PROFILE_MEDIUM: "PROFILE_MEDIUM",
    PROFILE_HIGH: "PROFILE_HIGH",
    PROFILE_MAX: "PROFILE_MAX",
    SATURATED: "SATURATED",
    DECAY: "DECAY",
  });

  // =========================
  // Configuration
  // =========================
  // Trigger thresholds
  lowThreshold;
  mediumThreshold;
  highThreshold;
  maxThreshold;
  // Release / hysteresis thresholds
  lowReleaseThreshold;
  mediumReleaseThreshold;
  highReleaseThreshold;
  maxReleaseThreshold;
  // Saturation timeout
  saturationTimeout;

  // =========================
  // Runtime state
  // =========================
  state;
  leftTrigger;
  rightTrigger;
  previousLeftTrigger;
  previousRightTrigger;
  stateEnterTime;
  saturationStartTime;
  lastUpdateTime;

  // =========================
  // Haptic profiles
  // =========================
  profiles;

  // =========================
  // Constructor
  // =========================
  constructor() {
    this.state = {
      left: TriggerHapticController.State.IDLE,
      right: TriggerHapticController.State.IDLE,
    };

    this.leftTrigger = 0;
    this.rightTrigger = 0;

    this.previousLeftTrigger = 0;
    this.previousRightTrigger = 0;

    this.stateEnterTime = {
      left: 0,
      right: 0,
    };

    this.saturationStartTime = {
      left: null,
      right: null,
    };

    this.profiles = {
      LOW: {},
      MEDIUM: {},
      HIGH: {},
      MAX: {},
    };
  }

  // =========================
  // Main update
  // =========================
  update(leftTrigger, rightTrigger, timestamp) {
    this.leftTrigger = leftTrigger;
    this.rightTrigger = rightTrigger;

    this.updateTrigger("left", leftTrigger, timestamp);

    this.updateTrigger("right", rightTrigger, timestamp);

    this.previousLeftTrigger = leftTrigger;
    this.previousRightTrigger = rightTrigger;
  }

  updateTrigger(side, value, timestamp) {
    // State machine logic will go here

    switch (this.state[side]) {
      case TriggerHapticController.State.IDLE:
        // ...
        break;

      case TriggerHapticController.State.PROFILE_LOW:
        // ...
        break;

      case TriggerHapticController.State.PROFILE_MEDIUM:
        // ...
        break;

      case TriggerHapticController.State.PROFILE_HIGH:
        // ...
        break;

      case TriggerHapticController.State.PROFILE_MAX:
        // ...
        break;

      case TriggerHapticController.State.SATURATED:
        // ...
        break;

      case TriggerHapticController.State.DECAY:
        // ...
        break;
    }
  }

  // =========================
  // State management
  // =========================
  transitionTo(side, newState) {
    this.state[side] = newState;
    // Apply/stop haptic output here
  }
  getState() {}

  // =========================
  // Profile handling
  // =========================
  getProfile(state) {}
  applyProfile(profile) {}

  // =========================
  // Threshold handling
  // =========================
  getProfileFromInput(value) {}
  getReleaseState(value) {}
  isAtSaturation(value) {}
  isReleased(value) {}

  // =========================
  // Saturation handling
  // =========================
  startSaturationTimer(timestamp) {
    //
  }
  isSaturationTimeout(timestamp) {}
  enterSaturatedState() {}

  // =========================
  // Decay / release
  // =========================
  enterDecayState() {
    //
  }
  updateDecay(value) {
    //
  }
  getDecayProfile(value) {
    //
  }

  // =========================
  // Output
  // =========================
  stopVibration() {
    //
  }
  outputVibration(profile) {
    //
  }
}
