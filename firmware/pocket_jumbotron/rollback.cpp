// Overrides the ESP32 core's weak hook (C linkage). Returning true means the core does NOT auto-confirm
// a freshly installed firmware; markAppValid() in portal.ino confirms it once startup succeeded.
extern "C" bool verifyRollbackLater() { return true; }
