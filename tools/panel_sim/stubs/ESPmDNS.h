// A stand-in for ESPmDNS: nothing.
//
// mDNS makes the panel answer on rct-panel.local inside the network. A build machine
// on the loopback interface has no such name to answer to, and pretending otherwise
// would mean inventing one - so the three calls are here and do nothing.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_MDNS_H
#define RCT_PANEL_SIM_MDNS_H

class MDNSClass {
 public:
  bool begin(const char *) { return true; }
  void addService(const char *, const char *, uint16_t) {}
  void end() {}
};
extern MDNSClass MDNS;

#endif // RCT_PANEL_SIM_MDNS_H
