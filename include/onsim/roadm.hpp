// Simulated N-degree ROADM: wavelength cross-connects, per-port optical power,
// insertion loss, and loss-of-signal alarms.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace onsim {

struct CrossConnect {
    int inPort = 0;
    int outPort = 0;
    int channel = 0;  // WavelengthGrid channel index
    std::string name; // operator-assigned identifier
};

struct PortState {
    bool enabled = true;
    double inputPowerDbm = -10.0;   // received power at the port
    double outputPowerDbm = -60.0;  // computed after cross-connect + loss
    bool losAlarm = false;          // loss of signal
};

// Simulated reconfigurable optical add-drop multiplexer.
//
// Model (intentionally simple, but with real ROADM semantics):
//  - `degrees` line ports, numbered 1..N.
//  - A cross-connect routes one wavelength channel from an input port to an
//    output port; a channel may appear at most once per output port
//    (wavelength-collision rule) and once per input port.
//  - Output power = input power - insertion loss; below `losThresholdDbm`
//    raises a LOS alarm on the output port.
class RoadmDevice {
public:
    explicit RoadmDevice(int degrees, double insertionLossDb = 6.0,
                         double losThresholdDbm = -28.0, uint64_t seed = 42);

    int degrees() const { return degrees_; }

    // --- provisioning (management plane calls into this) ---
    // Returns false (and sets lastError) on invalid port/channel or collision.
    bool addCrossConnect(const CrossConnect& xc);
    bool deleteCrossConnect(const std::string& name);
    std::vector<CrossConnect> crossConnects() const;

    bool setPortEnabled(int port, bool enabled);
    bool setInputPower(int port, double dbm);  // test hook / upstream change

    // --- state (telemetry reads from this) ---
    std::optional<PortState> portState(int port) const;
    std::string lastError() const { return lastError_; }

    // Advance the simulation: applies a small deterministic power drift and
    // re-evaluates output powers and LOS alarms.
    void tick();

private:
    bool validPort(int port) const { return port >= 1 && port <= degrees_; }
    void reevaluate();

    int degrees_;
    double insertionLossDb_;
    double losThresholdDbm_;
    uint64_t rngState_;
    std::map<int, PortState> ports_;
    std::map<std::string, CrossConnect> xcByName_;
    std::string lastError_;
};

}  // namespace onsim
