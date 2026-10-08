// Reading and writing the simulator's own settings file. See sim_config.h for what
// it is for and why it is not in the repository.

#include "sim_config.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/device/Json.h"

namespace {

// One flat JSON object with string and boolean values - the same shape the firmware's
// own device JSON has, so the parser that is already in the tree reads this too and
// no second one is needed.
const char *kDatei = "data/simulator.json";

// Set for a run that only takes a picture, so the save below writes nothing. The
// device of such a run is on its command line and belongs to that one frame.
bool g_gesperrt = false;

// Where the file really is, worked out once and then remembered.
//
// It belongs to the SIMULATOR's directory, not to the directory the simulator was
// started from. That difference was invisible as long as every start came from
// tools/panel_sim, and then it cost an afternoon: the settings path was relative, the
// file was looked for at <repo>/data/simulator.json when the program was started from
// the repo root, that directory does not exist - so the load found nothing and answered
// with the default (SIM), and every save said "Simulator-Einstellungen nicht
// schreibbar". A device chosen in the settings page was therefore never written down,
// the save answered "gespeichert", and the restart that the save itself triggers came
// back on the emulated inverter.
//
// The three places the file may be, in the order they are tried:
//   1. data/simulator.json next to the caller - an installation that keeps its data
//      somewhere else keeps it there, and nothing else may second-guess it
//   2. the same next to the program
//   3. the same next to the program's PARENT, which is the case for a direct start of
//      build/panel_sim: the binary sits in build/, the data one level above
// /proc/self/exe is Linux. Where it does not exist the list is just the relative path,
// which is where it always was.
std::vector<std::string> kandidaten() {
  std::vector<std::string> k;
  k.emplace_back(kDatei);
  char puffer[4096];
  const ssize_t n = readlink("/proc/self/exe", puffer, sizeof(puffer) - 1);
  if (n > 0) {
    puffer[n] = '\0';
    const std::string progmpfad(puffer);
    const size_t slash = progmpfad.rfind('/');
    if (slash != std::string::npos) {
      const std::string dir = progmpfad.substr(0, slash);
      k.push_back(dir + "/" + kDatei);
      const size_t eltern = dir.rfind('/');
      if (eltern != std::string::npos) {
        k.push_back(dir.substr(0, eltern) + "/" + kDatei);
      }
    }
  }
  return k;
}

bool istVerzeichnis(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string uebergeordneter(const std::string &pfad) {
  const size_t slash = pfad.rfind('/');
  return (slash == std::string::npos) ? std::string(".") : pfad.substr(0, slash);
}

std::string ermittlePfad() {
  static std::string pfad;
  if (!pfad.empty()) {
    return pfad;
  }
  for (const std::string &k : kandidaten()) {
    FILE *f = fopen(k.c_str(), "rb");
    if (f != nullptr) {
      fclose(f);
      pfad = k;
      return pfad;
    }
  }
  // No file anywhere yet. A directory that already holds a data folder is where this
  // program keeps its data: tools/panel_sim has one, build/ has none - which is why a
  // direct start of the binary finds its way one level up instead of writing a second
  // copy next to itself. Failing that, the first directory that exists at all.
  for (const std::string &k : kandidaten()) {
    if (istVerzeichnis(uebergeordneter(k) + "/data")) {
      pfad = k;
      return pfad;
    }
  }
  for (const std::string &k : kandidaten()) {
    if (istVerzeichnis(uebergeordneter(k))) {
      pfad = k;
      return pfad;
    }
  }
  pfad = kDatei;
  return pfad;
}

}  // namespace

void simSpeichernSperren(bool gesperrt) { g_gesperrt = gesperrt; }

bool simSpeichernGesperrt() { return g_gesperrt; }

std::string simConfigPfad() { return ermittlePfad(); }

bool simConfigLoad(SimConfig *cfg) {
  const std::string pfad = ermittlePfad();
  FILE *f = fopen(pfad.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  std::string s;
  char puffer[1024];
  size_t n;
  while ((n = fread(puffer, 1, sizeof(puffer), f)) > 0) {
    s.append(puffer, n);
  }
  fclose(f);

  const char *j = s.data();
  const size_t len = s.size();
  // getString writes into a fixed buffer; the struct holds std::string, so each
  // value is read into a local and moved over.
  char puffer2[64];
  auto text = [&](const char *key, std::string *ziel) {
    if (json::getString(j, len, key, puffer2, sizeof(puffer2))) {
      *ziel = puffer2;
    }
  };
  text("deviceType", &cfg->deviceType);
  text("deviceHost", &cfg->deviceHost);
  text("devicePort", &cfg->devicePort);
  text("dataFile", &cfg->dataFile);
  text("size", &cfg->size);
  // The theme is written as true or false, and json::getNumber does not read a boolean -
  // on purpose, because a device that answers "true" where a power belongs should not be
  // read as 1 (see src/device/Json.h). So this one key is read out of the text: the key,
  // then the word behind its colon. Both forms are accepted, because a file that a human
  // wrote by hand says either, and the one that is silently ignored would be the one that
  // costs an hour. An absent key leaves the default alone.
  {
    const std::string schluessel = "\"themeHell\"";
    const size_t at = s.find(schluessel);
    if (at != std::string::npos) {
      const size_t kolon = s.find(':', at + schluessel.size());
      const size_t woert =
          (kolon == std::string::npos)
              ? std::string::npos
              : s.find_first_not_of(" \t\r\n", kolon + 1);
      if (woert != std::string::npos) {
        cfg->themeHell =
            (s.compare(woert, 4, "true") == 0) || (s.compare(woert, 1, "1") == 0);
      }
    }
  }
  return true;
}

bool simConfigSave(const SimConfig &cfg) {
  if (g_gesperrt) {
    printf("Simulator-Einstellungen bleiben unveraendert (Aufnahme-Lauf)\n");
    return false;
  }
  const std::string pfad = ermittlePfad();
  FILE *f = fopen(pfad.c_str(), "wb");
  if (f == nullptr) {
    fprintf(stderr, "Simulator-Einstellungen nicht schreibbar: %s\n", pfad.c_str());
    return false;
  }
  // Written by hand rather than through a serialiser: five keys, and the firmware
  // has no JSON writer for objects either - its writers all emit one particular
  // answer.
  fprintf(f, "{\n");
  fprintf(f, "  \"deviceType\": \"%s\",\n", cfg.deviceType.c_str());
  fprintf(f, "  \"deviceHost\": \"%s\",\n", cfg.deviceHost.c_str());
  fprintf(f, "  \"devicePort\": \"%s\",\n", cfg.devicePort.c_str());
  fprintf(f, "  \"themeHell\": %s,\n", cfg.themeHell ? "true" : "false");
  fprintf(f, "  \"dataFile\": \"%s\",\n", cfg.dataFile.c_str());
  fprintf(f, "  \"size\": \"%s\"\n", cfg.size.c_str());
  fprintf(f, "}\n");
  fclose(f);
  printf("Simulator-Einstellungen gespeichert: %s\n", pfad.c_str());
  return true;
}