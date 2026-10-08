// Reading and writing the simulator's own settings file. See sim_config.h for what
// it is for and why it is not in the repository.

#include "sim_config.h"

#include <cstdio>
#include <cstring>

#include "../../src/device/Json.h"

namespace {

// One flat JSON object with string and boolean values - the same shape the firmware's
// own device JSON has, so the parser that is already in the tree reads this too and
// no second one is needed.
const char *kDatei = "data/simulator.json";

}  // namespace

std::string simConfigPfad() { return kDatei; }

bool simConfigLoad(SimConfig *cfg) {
  FILE *f = fopen(kDatei, "rb");
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
  FILE *f = fopen(kDatei, "wb");
  if (f == nullptr) {
    fprintf(stderr, "Simulator-Einstellungen nicht schreibbar: %s\n", kDatei);
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
  printf("Simulator-Einstellungen gespeichert: %s\n", kDatei);
  return true;
}