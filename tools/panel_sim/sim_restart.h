// Restarting the simulator. See sim_restart.cpp.
//
// Two things distinguish the two callers, and the difference is the whole of it:
//
//   after a device change   the device arguments are DROPPED, because the command
//                           line would otherwise override the choice that was just
//                           made and the switch would appear to do nothing
//   after a plain restart    they are KEPT, because the person typing them meant it
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_RESTART_H
#define RCT_PANEL_SIM_RESTART_H

// Remember argv, so that a restart can be execv()ed with the same arguments.
void simMerkeArgumente(int argc, char **argv);

// Restart. Does not return on success; returns false only if execv() failed.
bool simRestart(bool filterGeraet);

#endif // RCT_PANEL_SIM_RESTART_H
