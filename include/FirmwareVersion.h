// The panel firmware's own version, in one place.
//
// It was a literal in src/web/WebServer.cpp and a second literal in the browser
// installer's manifest, and the two could disagree - which is the failure mode
// version numbers exist to prevent. One header, and everything that shows or names
// a release reads it from here: the web interface, and tools/pack_web_install.sh,
// which writes it into the installer's manifests and page.
//
// What the number is NOT: it says nothing about the inverter's own firmware. That
// is the backend's version, it arrives with every data set and is shown on the
// Info page. See the note next to DeviceState::firmwareVersion - two versions, two
// sources, and confusing them is the mistake this header exists next to.
//
//   RCT_FW_VERSION      the release, raised when the panel's behaviour changes.
//                       A prerelease carries a suffix ("1.0-rc1") and is tagged with
//                       exactly this string prefixed by "v" - the panel, the
//                       installer's manifests and the tag then name one thing.
//   RCT_FW_VERSION_MON  when it was, so a support case can be placed in time
//
// The language is deliberately NOT part of it. The two builds are the same release
// of the same firmware in two languages, and a version that differed by language
// would make it look like two products. The language is shown next to it.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_FW_VERSION_H
#define RCT_FW_VERSION_H

#define RCT_FW_VERSION "1.0-rc1"
#define RCT_FW_VERSION_MON "2026-10"

// Both parts at once, for the places that print the version as one string.
#define RCT_FW_VERSION_FULL RCT_FW_VERSION " (" RCT_FW_VERSION_MON ")"

#endif  // RCT_FW_VERSION_H