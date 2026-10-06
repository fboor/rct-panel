// Restarting the simulator the way the panel restarts.
//
// A panel saves the new device and calls ESP.restart(). A build machine cannot do
// that - the process would die and the window with it, and nobody would get their
// emulator back - so the first version only restarted the DRIVER. That is why it
// never exercised the thing that matters: on a panel the theme comes out of the NVS
// at boot, the driver begins at boot, and the first values are the first thing you
// see. Changing the device without a restart shows a state the panel never has.
//
// So the simulator restarts itself: execv() with the same binary and the same
// arguments, minus the arguments that named the device.
//
// WHY THE FILTER IS NOT OPTIONAL
//
// The precedence is command line over saved file over default. A restart with the
// same argv would therefore undo the switch:
//
//   start with --device RCT --host 192.168.1.83
//   switch to OIG in the browser -> the saved file says OIG
//   restart with the same argv   -> the command line says RCT, and it wins
//
// The switch would appear to do nothing, and it would look like the feature is
// broken. So on a restart caused by a device change the device arguments go, and the
// saved file is what decides. Everything else - --size, --lang, --data, --page,
// --shot - is a property of HOW the emulator is looked at, not of WHICH device it
// talks to, so it stays and the run comes back the way it was asked for.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "sim_config.h"
#include "sim_stubs.h"

namespace {

// The arguments as they were handed to us. The program owns them - it did not copy
// them and it must not free them either.
int g_argc = 0;
char **g_argv = nullptr;
int g_neustarts = 0;

// Three is enough for a person and one too many for a loop.
const int kMaxNeustarts = 3;

// The arguments that name the device, and the ones that must survive a restart
// because they say how the emulator is being looked at rather than what it talks to.
bool istGeraeteArgument(const std::string &a) {
  return a == "--device" || a == "--host" || a == "--port";
}

bool istGeraeteArgumentMitWert(const std::string &a) {
  return a.rfind("--device=", 0) == 0 || a.rfind("--host=", 0) == 0 ||
         a.rfind("--port=", 0) == 0;
}

}  // namespace

void simMerkeArgumente(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
  // The brake. A program that restarts itself can restart itself for ever, and the
  // failure mode is a window that blinks and a machine that gets busy - not an error
  // anybody would think to look for.
  //
  // So the count travels in the environment, which execv() passes on, and past the
  // limit the restart is refused and the emulator simply runs with what it has. The
  // count resets when the process is started from a shell, so a real session is
  // never affected: three restarts in a row means something is wrong, and running
  // anyway is better than not coming up.
  const char *alt = getenv("RCT_SIM_RESTARTS");
  g_neustarts = (alt != nullptr) ? atoi(alt) : 0;
}

// Restart the process. `filterGeraet` says whether the device arguments go with it:
// true after a device change, false for a plain restart where the command line is
// still what the user typed and means.
bool simRestart(bool filterGeraet) {
  if (g_neustarts >= kMaxNeustarts) {
    // Careful with the wording, because the first version of this line said "it runs
    // with what is there" - and it does not: the device has already been switched by
    // the time this is reached. What did NOT happen is the restart, and that is the
    // part that matters: this state has not been through a boot, so it is not the
    // state a panel would be in. Saying it the other way round would have made the
    // brake look like a feature.
    fprintf(stderr,
            "Simulator: kein Neustart mehr, %d in Folge waren es. Der Treiber ist "
            "umgestellt, der Prozess aber NICHT neu gestartet - dieser Zustand ist "
            "also nicht durch einen Boot gegangen. Simulator beenden und neu "
            "starten.\n",
            g_neustarts);
    return false;
  }
  printf("Simulator: Neustart%s (%d von %d)\n",
         filterGeraet ? ", Geraeteargumente fallen weg" : "",
         g_neustarts + 1, kMaxNeustarts);

  // The window has to go before the port is handed over, or the new process finds
  // 8081 occupied by the old one for as long as it takes the kernel to release it.
  simQuit();

  std::vector<char *> neu;
  neu.push_back(g_argv[0]);
  for (int i = 1; i < g_argc; i++) {
    const std::string a = g_argv[i];
    if (filterGeraet) {
      if (istGeraeteArgument(a)) {
        i++;   // and its value
        continue;
      }
      if (istGeraeteArgumentMitWert(a)) {
        continue;
      }
    }
    neu.push_back(g_argv[i]);
  }
  neu.push_back(nullptr);

  if (filterGeraet) {
    printf("Simulator: neu mit %d Argumenten\n", (int)neu.size() - 2);
  }
  char zaehler[8];
  snprintf(zaehler, sizeof(zaehler), "%d", g_neustarts + 1);
  setenv("RCT_SIM_RESTARTS", zaehler, 1);
  execv(g_argv[0], neu.data());
  // execv only returns on failure.
  fprintf(stderr, "execv fehlgeschlagen: %s\n", strerror(errno));
  return false;
}